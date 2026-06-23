#include "meshedit/MeshEditService.h"

#include "meshedit/MeshElements.h"
#include "brush/BrushOps.h"
#include "brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <set>
#include <utility>
#include <vector>

namespace
{
class CapturingCommand : public ICommand
{
public:
    CapturingCommand(BrushMesh before, BrushMesh after)
        : Before(std::move(before))
        , After(std::move(after))
    {
    }

    void Execute() override {}
    void Undo() override {}

    BrushMesh Before;
    BrushMesh After;
};

class StubMeshEditTarget : public IMeshEditTarget
{
public:
    explicit StubMeshEditTarget(BrushMesh mesh)
        : Mesh(std::move(mesh))
    {
    }

    std::optional<MeshEditTargetMesh> Resolve(EntityId entity) const override
    {
        if (!(entity == Entity))
            return std::nullopt;
        return MeshEditTargetMesh{
            .Mesh = &Mesh,
            .Transform = Transform3f::Identity(),
        };
    }

    std::unique_ptr<ICommand> MakeEditCommand(EntityId,
                                              BrushMesh before,
                                              BrushMesh after) override
    {
        return std::make_unique<CapturingCommand>(std::move(before), std::move(after));
    }

    EntityId Entity{ 1, 1 };
    BrushMesh Mesh;
};

SelectionSnapshot FaceSelection(EntityId entity, std::uint32_t face)
{
    const SelectableRef ref = SelectableRef::FaceSelection(RegistryId::Global(), entity, face);
    return SelectionSnapshot{ .Items = { ref }, .Primary = ref };
}
}

TEST(MeshEditService, ExtrudeProducesCommandWithEditedMesh)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;

    MeshEditParams params;
    params.Distance = 1.0f;
    std::unique_ptr<ICommand> command = service.ApplyVerb(
        target, FaceSelection(target.Entity, 0), MeshEditVerb::Extrude, params);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->Before.Faces.size(), 6u);
    EXPECT_GT(captured->After.Faces.size(), captured->Before.Faces.size());
}

TEST(MeshEditService, DeleteProducesCommandWithEditedMesh)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;

    std::unique_ptr<ICommand> command = service.ApplyVerb(
        target, FaceSelection(target.Entity, 0), MeshEditVerb::Delete);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->After.Faces.size(), 5u);
}

namespace
{
SelectionSnapshot FaceSelectionMulti(EntityId entity, std::initializer_list<std::uint32_t> faces)
{
    SelectionSnapshot snapshot;
    for (std::uint32_t face : faces)
        snapshot.Items.push_back(SelectableRef::FaceSelection(RegistryId::Global(), entity, face));
    if (!snapshot.Items.empty())
        snapshot.Primary = snapshot.Items.front();
    return snapshot;
}

std::size_t ExtrudedFaceCount(std::initializer_list<std::uint32_t> faces)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;
    auto command = service.ApplyVerb(
        target, FaceSelectionMulti(target.Entity, faces), MeshEditVerb::Extrude, { .Distance = 1.0f });
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    return captured != nullptr ? captured->After.Faces.size() : 0;
}
}

TEST(MeshEditService, MultiFaceExtrudeAppliesToEverySelectedFace)
{
    // Two faces must both extrude — more faces than extruding just one. (The old
    // index-based loop dropped the second face once the first repair reindexed.)
    EXPECT_GT(ExtrudedFaceCount({ 0, 1 }), ExtrudedFaceCount({ 0 }));
}

TEST(MeshEditService, MultiFaceExtrudeIsOrderIndependent)
{
    // Identity-resolved targets make the result independent of selection order.
    EXPECT_EQ(ExtrudedFaceCount({ 0, 1 }), ExtrudedFaceCount({ 1, 0 }));
    EXPECT_GT(ExtrudedFaceCount({ 0, 1 }), 0u);
}

TEST(MeshEditService, InvalidSelectionDoesNotProduceCommand)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;

    const SelectableRef ref = SelectableRef::EntitySelection(RegistryId::Global(), target.Entity);
    const SelectionSnapshot selection{ .Items = { ref }, .Primary = ref };

    EXPECT_EQ(service.ApplyVerb(target, selection, MeshEditVerb::Delete), nullptr);
}

namespace
{
EntityId kEntity{ 1, 1 };

std::vector<SelectableRef> VertexRefs(std::initializer_list<std::uint32_t> indices)
{
    std::vector<SelectableRef> refs;
    for (std::uint32_t index : indices)
        refs.push_back(SelectableRef::VertexSelection(RegistryId::Global(), kEntity, index));
    return refs;
}
}

TEST(MeshEditService, TranslateVertexMovesOnlyThatVertexLocally)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f });
    MeshEditService service;

    const Vec3d delta(1.0f, 2.0f, 3.0f);
    const std::vector<SelectableRef> refs = VertexRefs({ 0 });
    const std::optional<BrushMesh> after = service.TranslateElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Vertex, delta, false);

    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after->Vertices.size(), box.Vertices.size());
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
    {
        const Vec3d expected = (i == 0) ? box.Vertices[i].Position + delta
                                        : box.Vertices[i].Position;
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.X, expected.X);
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.Y, expected.Y);
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.Z, expected.Z);
    }
}

TEST(MeshEditService, TranslateFaceDeduplicatesSharedVertices)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f });
    MeshEditService service;

    const std::vector<std::uint32_t>& loop = box.Faces[0].Loop;
    const Vec3d delta(0.0f, 0.0f, 1.5f);
    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, 0),
    };
    const std::optional<BrushMesh> after = service.TranslateElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Face, delta, false);

    ASSERT_TRUE(after.has_value());
    int moved = 0;
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
    {
        const bool inLoop = std::find(loop.begin(), loop.end(), static_cast<std::uint32_t>(i)) != loop.end();
        const Vec3d expected = inLoop ? box.Vertices[i].Position + delta : box.Vertices[i].Position;
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.Z, expected.Z);
        if (inLoop)
            ++moved;
    }
    EXPECT_EQ(static_cast<std::size_t>(moved), loop.size());
}

TEST(MeshEditService, TranslateConvertsWorldDeltaThroughTransformScale)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f });
    MeshEditService service;

    Transform3f transform = Transform3f::Identity();
    transform.Scale = { 2.0f, 2.0f, 2.0f };

    const Vec3d worldDelta(2.0f, 0.0f, 0.0f); // local should be halved by the scale
    const std::vector<SelectableRef> refs = VertexRefs({ 0 });
    const std::optional<BrushMesh> after = service.TranslateElements(
        box, transform, refs, MeshElementKind::Vertex, worldDelta, false);

    ASSERT_TRUE(after.has_value());
    EXPECT_FLOAT_EQ(after->Vertices[0].Position.X, box.Vertices[0].Position.X + 1.0f);
}

TEST(MeshEditService, TranslateElementsVerbProducesValidatedCommand)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f }));
    MeshEditService service;
    service.SetElementKind(MeshElementKind::Vertex);

    MeshEditParams params;
    params.TranslateDelta = Vec3d(0.5f, 0.0f, 0.0f);
    const SelectableRef ref = SelectableRef::VertexSelection(RegistryId::Global(), target.Entity, 0);
    const SelectionSnapshot selection{ .Items = { ref }, .Primary = ref };

    std::unique_ptr<ICommand> command =
        service.ApplyVerb(target, selection, MeshEditVerb::TranslateElements, params);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->After.Vertices.size(), 8u);
}

TEST(MeshEditService, InsertEdgeLoopOnBoxProducesClosedManifold)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    StubMeshEditTarget target(box);
    MeshEditService service;
    service.SetElementKind(MeshElementKind::Edge);

    const std::vector<EdgeElement> edges = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_FALSE(edges.empty());

    const SelectableRef ref =
        SelectableRef::EdgeSelection(RegistryId::Global(), target.Entity, edges[0].Index);
    const SelectionSnapshot selection{ .Items = { ref }, .Primary = ref };

    std::unique_ptr<ICommand> command =
        service.ApplyVerb(target, selection, MeshEditVerb::InsertEdgeLoop);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    // Any seed edge on a box drives a ring of 4 faces (4 sides, or 2 sides + 2 caps);
    // the ring crosses 4 edges, so 4 midpoints and 4 extra faces, staying closed.
    EXPECT_EQ(captured->After.Vertices.size(), box.Vertices.size() + 4);
    EXPECT_EQ(captured->After.Faces.size(), box.Faces.size() + 4);

    BrushMesh check = captured->After;
    const BrushRepairResult repair = BrushValidateAndRepair(check);
    EXPECT_TRUE(repair.Ok);
    EXPECT_TRUE(repair.Closed);
}

namespace
{
// A flat open strip of three quads in a row (8 vertices, 3 faces), CCW from +Z.
// Used to prove the loop spans both directions from a middle seed edge.
BrushMesh ThreeQuadStrip()
{
    BrushMesh mesh;
    mesh.Vertices = {
        BrushVertex{ { 0, 0, 0 } }, BrushVertex{ { 1, 0, 0 } },
        BrushVertex{ { 2, 0, 0 } }, BrushVertex{ { 3, 0, 0 } },
        BrushVertex{ { 0, 1, 0 } }, BrushVertex{ { 1, 1, 0 } },
        BrushVertex{ { 2, 1, 0 } }, BrushVertex{ { 3, 1, 0 } },
    };
    mesh.Faces = {
        BrushFace{ { 0, 1, 5, 4 }, {} },
        BrushFace{ { 1, 2, 6, 5 }, {} },
        BrushFace{ { 2, 3, 7, 6 }, {} },
    };
    BrushValidateAndRepair(mesh);
    return mesh;
}
}

TEST(MeshEditService, InsertEdgeLoopSpansBothDirectionsFromMiddleSeed)
{
    // The regression this hardening fixes: the old one-directional walk cut only the
    // half of the loop on one side of the seed (and stranded midpoints, opening the
    // mesh). Seeding the MIDDLE edge {1,5} must split all three quads, not one or two.
    const BrushMesh strip = ThreeQuadStrip();

    const BrushMesh after = BrushOps::InsertEdgeLoop(strip, 1, 5);

    EXPECT_EQ(after.Vertices.size(), strip.Vertices.size() + 4); // 4 rungs cut
    EXPECT_EQ(after.Faces.size(), strip.Faces.size() + 3);       // all 3 quads split

    BrushMesh check = after;
    EXPECT_TRUE(BrushValidateAndRepair(check).Ok); // no T-junctions, valid geometry
}

TEST(MeshEditService, InsertEdgeLoopAbsentSeedReturnsUnchanged)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });

    // Vertices 0 and 6 are diagonally opposite corners: not a real edge.
    const BrushMesh after = BrushOps::InsertEdgeLoop(box, 0, 6);

    EXPECT_EQ(after.Vertices.size(), box.Vertices.size());
    EXPECT_EQ(after.Faces.size(), box.Faces.size());
}

TEST(MeshEditService, InsertEdgeLoopNeverCorruptsMeshForAnySeed)
{
    // Predictability contract: for EVERY edge of several mesh shapes, the loop cut is
    // either a clean no-op or a valid manifold edit. It never produces broken
    // geometry, and it never opens a mesh that was closed.
    BrushMesh openBox = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    openBox.Faces.erase(openBox.Faces.begin()); // a hole, like a carved doorway wall
    BrushValidateAndRepair(openBox);

    const std::vector<BrushMesh> shapes = {
        BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }),
        BrushOps::MakeCylinder({ 1.0f, 1.0f, 1.0f }, 1, 8),
        openBox,
        ThreeQuadStrip(),
    };

    for (const BrushMesh& shape : shapes)
    {
        BrushMesh closedCheck = shape;
        const bool inputClosed = BrushValidateAndRepair(closedCheck).Closed;

        for (const EdgeElement& edge : MeshElements::Edges(shape, Transform3f::Identity()))
        {
            BrushMesh after = BrushOps::InsertEdgeLoop(shape, edge.VertexA, edge.VertexB);
            const BrushRepairResult repair = BrushValidateAndRepair(after);

            EXPECT_TRUE(repair.Ok) << "seed edge " << edge.VertexA << "-" << edge.VertexB;
            if (after.Faces.size() != shape.Faces.size())
            {
                EXPECT_GT(after.Vertices.size(), shape.Vertices.size());
                EXPECT_GT(after.Faces.size(), shape.Faces.size());
            }
            if (inputClosed)
                EXPECT_TRUE(repair.Closed) << "loop cut opened a closed mesh";
        }
    }
}

TEST(MeshEditService, InsertEdgeLoopRepeatsStably)
{
    // Mimics the user's multi-step workflow: stack several loop cuts and confirm the
    // mesh stays a valid closed manifold the whole way (no drift into broken state).
    BrushMesh mesh = BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f });

    for (int pass = 0; pass < 5; ++pass)
    {
        const std::vector<EdgeElement> edges = MeshElements::Edges(mesh, Transform3f::Identity());
        ASSERT_FALSE(edges.empty());
        const EdgeElement& seed = edges[pass % edges.size()];

        BrushMesh after = BrushOps::InsertEdgeLoop(mesh, seed.VertexA, seed.VertexB);
        const BrushRepairResult repair = BrushValidateAndRepair(after);
        ASSERT_TRUE(repair.Ok) << "pass " << pass;
        EXPECT_TRUE(repair.Closed) << "pass " << pass;
        mesh = after;
    }
}

namespace
{
// A flat NxN grid of unit quads in the XY plane, vertex index = j*(N+1)+i. Interior
// vertices are valence 4, so it exercises the edge-loop walk that primitives (all
// valence-3 corners) cannot.
BrushMesh QuadGrid(int n)
{
    const int stride = n + 1;
    BrushMesh mesh;
    for (int j = 0; j < stride; ++j)
        for (int i = 0; i < stride; ++i)
            mesh.Vertices.push_back(BrushVertex{ { static_cast<float>(i), static_cast<float>(j), 0.0f } });
    for (int gj = 0; gj < n; ++gj)
        for (int gi = 0; gi < n; ++gi)
        {
            const std::uint32_t v00 = static_cast<std::uint32_t>(gj * stride + gi);
            mesh.Faces.push_back(BrushFace{
                { v00, v00 + 1, v00 + 1 + stride, v00 + stride }, {} });
        }
    BrushValidateAndRepair(mesh);
    return mesh;
}

std::set<std::array<std::uint32_t, 2>> EdgeSet(const std::vector<std::array<std::uint32_t, 2>>& edges)
{
    return { edges.begin(), edges.end() };
}
}

TEST(BrushOps, TraceEdgeLoopFollowsRowThroughInteriorVertices)
{
    const BrushMesh grid = QuadGrid(3); // 4x4 verts; (1,1)=5 and (2,1)=6 are interior
    // Seed the middle horizontal edge of row j=1. The loop continues straight across
    // the two interior (valence-4) vertices and stops at the boundary poles: the
    // whole j=1 row, {4,5},{5,6},{6,7}.
    const std::vector<std::array<std::uint32_t, 2>> loop = BrushOps::TraceEdgeLoop(grid, 5, 6);
    EXPECT_EQ(EdgeSet(loop),
              (std::set<std::array<std::uint32_t, 2>>{ { 4, 5 }, { 5, 6 }, { 6, 7 } }));
}

TEST(BrushOps, TraceEdgeLoopStopsAtValenceThreePrimitiveVertices)
{
    // Every box vertex is valence 3, so no edge loop extends: the seed alone.
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<EdgeElement> edges = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_FALSE(edges.empty());
    const std::vector<std::array<std::uint32_t, 2>> loop =
        BrushOps::TraceEdgeLoop(box, edges[0].VertexA, edges[0].VertexB);
    EXPECT_EQ(loop.size(), 1u);
}

TEST(BrushOps, TraceEdgeLoopSelectsInsertedLoopAfterCut)
{
    // The core workflow: a loop cut makes valence-4 midpoints, so the inserted loop
    // then selects as a whole. Cut a box, seed one of the new (midpoint-to-midpoint)
    // edges, and expect the full closed ring of 4 inserted edges.
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<EdgeElement> seed = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_FALSE(seed.empty());
    BrushMesh cut = BrushOps::InsertEdgeLoop(box, seed[0].VertexA, seed[0].VertexB);
    BrushValidateAndRepair(cut);

    std::optional<std::pair<std::uint32_t, std::uint32_t>> loopSeed;
    for (const EdgeElement& edge : MeshElements::Edges(cut, Transform3f::Identity()))
        if (edge.VertexA >= box.Vertices.size() && edge.VertexB >= box.Vertices.size())
        {
            loopSeed = { edge.VertexA, edge.VertexB };
            break;
        }
    ASSERT_TRUE(loopSeed.has_value());

    const std::vector<std::array<std::uint32_t, 2>> loop =
        BrushOps::TraceEdgeLoop(cut, loopSeed->first, loopSeed->second);
    EXPECT_EQ(loop.size(), 4u);
}

TEST(BrushOps, TraceEdgeLoopFollowsCylinderCapRim)
{
    // A capped cylinder's rim vertex is a valence-3 fan (cap ngon + 2 side quads),
    // topologically the same as a pole; the geometric fallback carries the loop all
    // the way around the rim. Regression for "alt+click on the top rim selects nothing".
    const int sides = 8;
    const BrushMesh cyl = BrushOps::MakeCylinder({ 1.0f, 1.0f, 1.0f }, 1, sides);

    std::optional<std::pair<std::uint32_t, std::uint32_t>> rim;
    for (const EdgeElement& edge : MeshElements::Edges(cyl, Transform3f::Identity()))
        if (cyl.Vertices[edge.VertexA].Position.Y > 0.9f && cyl.Vertices[edge.VertexB].Position.Y > 0.9f)
        {
            rim = { edge.VertexA, edge.VertexB };
            break;
        }
    ASSERT_TRUE(rim.has_value());

    const std::vector<std::array<std::uint32_t, 2>> loop = BrushOps::TraceEdgeLoop(cyl, rim->first, rim->second);
    EXPECT_EQ(loop.size(), static_cast<std::size_t>(sides));
}

TEST(BrushOps, TraceEdgeLoopOnCylinderVerticalEdgeStaysSingle)
{
    // A vertical side edge is a "rung", not a loop: at the rim the only straighter-than-
    // 90-degree continuation would be another vertical, and there is none, so it stops.
    const BrushMesh cyl = BrushOps::MakeCylinder({ 1.0f, 1.0f, 1.0f }, 1, 8);

    std::optional<std::pair<std::uint32_t, std::uint32_t>> vertical;
    for (const EdgeElement& edge : MeshElements::Edges(cyl, Transform3f::Identity()))
    {
        const float ya = cyl.Vertices[edge.VertexA].Position.Y;
        const float yb = cyl.Vertices[edge.VertexB].Position.Y;
        if ((ya > 0.9f) != (yb > 0.9f)) // one endpoint top, one bottom
        {
            vertical = { edge.VertexA, edge.VertexB };
            break;
        }
    }
    ASSERT_TRUE(vertical.has_value());

    const std::vector<std::array<std::uint32_t, 2>> loop =
        BrushOps::TraceEdgeLoop(cyl, vertical->first, vertical->second);
    EXPECT_EQ(loop.size(), 1u);
}

TEST(BrushOps, TraceEdgeRingMatchesInsertEdgeLoopStrip)
{
    const BrushMesh grid = QuadGrid(3);
    const BrushOps::BrushEdgeRing ring = BrushOps::TraceEdgeRing(grid, 5, 6);
    // Seed {5,6} is horizontal; the ring crosses the vertical column of 3 quads and
    // its 4 horizontal rungs. The strip is exactly what InsertEdgeLoop splits (both
    // share the flood-fill), so the cut adds one face per strip face.
    EXPECT_EQ(ring.StripFaces.size(), 3u);
    EXPECT_EQ(ring.RingEdges.size(), 4u);
    const BrushMesh cut = BrushOps::InsertEdgeLoop(grid, 5, 6);
    EXPECT_EQ(cut.Faces.size(), grid.Faces.size() + ring.StripFaces.size());
}

TEST(BrushOps, TraceEdgeRingFaceLoopOnBox)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    const std::vector<EdgeElement> edges = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_FALSE(edges.empty());
    const BrushOps::BrushEdgeRing ring =
        BrushOps::TraceEdgeRing(box, edges[0].VertexA, edges[0].VertexB);
    EXPECT_EQ(ring.StripFaces.size(), 4u); // a belt of 4 faces around the box
}

TEST(BrushOps, TraceAbsentSeedReturnsEmpty)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    EXPECT_TRUE(BrushOps::TraceEdgeLoop(box, 0, 6).empty());        // diagonal, not an edge
    EXPECT_TRUE(BrushOps::TraceEdgeRing(box, 0, 6).StripFaces.empty());
}

TEST(MeshEditService, ResizeBoundsRemapsVerticesAffinely)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }); // verts at +/-1
    MeshEditService service;

    // Stretch +X to 2 (anchor the -X face), leave Y/Z alone.
    const std::optional<BrushMesh> after = service.ResizeBounds(
        box, Transform3f::Identity(),
        Vec3d(-1.0f, -1.0f, -1.0f), Vec3d(1.0f, 1.0f, 1.0f),
        Vec3d(-1.0f, -1.0f, -1.0f), Vec3d(2.0f, 1.0f, 1.0f),
        true);

    ASSERT_TRUE(after.has_value());
    ASSERT_EQ(after->Vertices.size(), box.Vertices.size());
    for (std::size_t i = 0; i < box.Vertices.size(); ++i)
    {
        // +X verts move to 2, -X verts stay at -1; Y/Z unchanged.
        const float expectedX = box.Vertices[i].Position.X > 0.0f ? 2.0f : -1.0f;
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.X, expectedX);
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.Y, box.Vertices[i].Position.Y);
        EXPECT_FLOAT_EQ(after->Vertices[i].Position.Z, box.Vertices[i].Position.Z);
    }
}

TEST(MeshEditService, TranslateWithNoMatchingElementsReturnsNullopt)
{
    const BrushMesh box = BrushOps::MakeBox({ 2.0f, 2.0f, 2.0f });
    MeshEditService service;

    // Face refs under Vertex mode resolve to no vertex indices.
    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, 0),
    };
    EXPECT_FALSE(service.TranslateElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Vertex, Vec3d(1.0f, 0.0f, 0.0f), false)
        .has_value());
}

TEST(MeshEditService, ExtrudeElementsFaceGrowsCapAndWalls)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    MeshEditService service;

    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, 0),
    };
    // Extrude outward along face 0's own normal (dragging inward would push the
    // cap into the opposing face and weld it away).
    const Vec3d outward = BrushComputeFaceNormal(box, box.Faces[0]).Normalized() * 2.0f;
    const auto after = service.ExtrudeElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Face, outward, true);

    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->Mesh.Vertices.size(), 12u); // 8 + 4 extruded ring
    EXPECT_EQ(after->Mesh.Faces.size(), 10u);    // 6 + 4 side walls

    // The new cap is reported and sits at the source centroid plus the offset.
    ASSERT_EQ(after->NewElementIds.size(), 1u);
    const Vec3d expectedCap = BrushFaceCentroid(box, box.Faces[0]) + outward;
    const std::uint32_t cap = after->NewElementIds[0];
    ASSERT_LT(cap, after->Mesh.Faces.size());
    const Vec3d capCentroid = BrushFaceCentroid(after->Mesh, after->Mesh.Faces[cap]);
    EXPECT_NEAR((capCentroid - expectedCap).SqrMagnitude(), 0.0f, 1e-6f);
}

TEST(MeshEditService, ExtrudeElementsEdgePullsNewPlane)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    MeshEditService service;

    const std::vector<EdgeElement> edges = MeshElements::Edges(box, Transform3f::Identity());
    ASSERT_FALSE(edges.empty());
    const Vec3d offset(0.0f, 0.0f, 3.0f);
    const std::vector<SelectableRef> refs = {
        SelectableRef::EdgeSelection(RegistryId::Global(), kEntity, edges[0].Index),
    };
    const auto after = service.ExtrudeElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Edge, offset, true);

    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->Mesh.Vertices.size(), 10u); // 8 + 2
    EXPECT_EQ(after->Mesh.Faces.size(), 7u);     // 6 + 1 strip

    // The new outer edge is reported; its endpoints are the source endpoints plus
    // the offset.
    ASSERT_EQ(after->NewElementIds.size(), 1u);
    const auto outer = MeshElements::TryGetEdge(after->Mesh, Transform3f::Identity(), after->NewElementIds[0]);
    ASSERT_TRUE(outer.has_value());
    const Vec3d na = edges[0].A + offset;
    const Vec3d nb = edges[0].B + offset;
    const bool matches = ((outer->A - na).SqrMagnitude() < 1e-6f && (outer->B - nb).SqrMagnitude() < 1e-6f)
                      || ((outer->A - nb).SqrMagnitude() < 1e-6f && (outer->B - na).SqrMagnitude() < 1e-6f);
    EXPECT_TRUE(matches);
}

TEST(MeshEditService, ExtrudeElementsBelowThresholdIsNullopt)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    MeshEditService service;

    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, 0),
    };
    // A drag that has not yet crossed the extrude threshold: no-op, not a
    // degenerate weld.
    EXPECT_FALSE(service.ExtrudeElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Face, Vec3d(0.0f, 0.0f, 1e-6f), true)
        .has_value());
}

TEST(MeshEditService, ExtrudeElementsMultiFaceReportsEveryCap)
{
    const BrushMesh box = BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f });
    MeshEditService service;

    std::uint32_t plusZ = 0;
    std::uint32_t plusX = 0;
    for (std::uint32_t i = 0; i < box.Faces.size(); ++i)
    {
        if (BrushComputeFaceNormal(box, box.Faces[i]).Z > 0.9f) plusZ = i;
        if (BrushComputeFaceNormal(box, box.Faces[i]).X > 0.9f) plusX = i;
    }
    const std::vector<SelectableRef> refs = {
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, plusZ),
        SelectableRef::FaceSelection(RegistryId::Global(), kEntity, plusX),
    };
    // A diagonal offset keeps both caps non-degenerate and non-colliding, so both
    // faces extrude and both caps are re-found by identity.
    const Vec3d offset(1.0f, 0.0f, 1.0f);
    const auto after = service.ExtrudeElements(
        box, Transform3f::Identity(), refs, MeshElementKind::Face, offset, true);

    ASSERT_TRUE(after.has_value());
    EXPECT_EQ(after->NewElementIds.size(), 2u);
    for (std::uint32_t cap : after->NewElementIds)
        EXPECT_LT(cap, after->Mesh.Faces.size());
}

TEST(MeshEditService, FlipFaceNormalReversesEverySelectedFace)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;

    const Vec3d n0 = BrushComputeFaceNormal(target.Mesh, target.Mesh.Faces[0]);
    const Vec3d n1 = BrushComputeFaceNormal(target.Mesh, target.Mesh.Faces[1]);

    std::unique_ptr<ICommand> command = service.ApplyVerb(
        target, FaceSelectionMulti(target.Entity, { 0, 1 }), MeshEditVerb::FlipFaceNormal);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    // Both selected faces now point the opposite way, and repair did not undo it.
    EXPECT_LT(BrushComputeFaceNormal(captured->After, captured->After.Faces[0]).Dot(n0), -0.9f);
    EXPECT_LT(BrushComputeFaceNormal(captured->After, captured->After.Faces[1]).Dot(n1), -0.9f);
}

TEST(MeshEditService, RecalculateNormalsReorientsFlippedBrush)
{
    // A box with one inward-flipped face; recalc must put every face outward.
    BrushMesh box = BrushOps::FlipFace(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }), 0);
    StubMeshEditTarget target(std::move(box));
    MeshEditService service;

    const SelectableRef ref = SelectableRef::EntitySelection(RegistryId::Global(), target.Entity);
    const SelectionSnapshot selection{ .Items = { ref }, .Primary = ref };

    std::unique_ptr<ICommand> command =
        service.ApplyVerb(target, selection, MeshEditVerb::RecalculateNormals);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    const Vec3d center = BrushMeshCentroid(captured->After);
    for (const BrushFace& face : captured->After.Faces)
        EXPECT_GT(BrushComputeFaceNormal(captured->After, face).Dot(
                      BrushFaceCentroid(captured->After, face) - center), 0.0f);
}

TEST(MeshEditService, RecalculateNormalsIgnoresFaceSelection)
{
    StubMeshEditTarget target(BrushOps::MakeBox({ 1.0f, 1.0f, 1.0f }));
    MeshEditService service;

    // The verb filters to entity refs, so a face selection yields no command.
    EXPECT_EQ(service.ApplyVerb(
                  target, FaceSelection(target.Entity, 0), MeshEditVerb::RecalculateNormals),
              nullptr);
}

namespace
{
// Two faces are part of one oriented surface iff they traverse their shared edge
// in opposite directions. Returns true only when a shared edge exists and is
// opposed; false if it is traversed the same way (a flipped/inconsistent face).
bool FacesContinueSurface(const BrushMesh& mesh, std::uint32_t f0, std::uint32_t f1)
{
    const std::vector<std::uint32_t>& a = mesh.Faces[f0].Loop;
    const std::vector<std::uint32_t>& b = mesh.Faces[f1].Loop;
    for (std::size_t i = 0; i < a.size(); ++i)
    {
        const std::uint32_t a0 = a[i];
        const std::uint32_t a1 = a[(i + 1) % a.size()];
        for (std::size_t j = 0; j < b.size(); ++j)
        {
            const std::uint32_t b0 = b[j];
            const std::uint32_t b1 = b[(j + 1) % b.size()];
            if (a0 == b1 && a1 == b0)
                return true; // opposed: consistent
            if (a0 == b0 && a1 == b1)
                return false; // same direction: inconsistent
        }
    }
    return false; // no shared edge found
}

// Index of the single +Y floor face in a plane-extrude result.
std::uint32_t FloorFace(const BrushMesh& mesh)
{
    for (std::uint32_t i = 0; i < mesh.Faces.size(); ++i)
        if (BrushComputeFaceNormal(mesh, mesh.Faces[i]).Y > 0.9f)
            return i;
    return 0;
}
}

TEST(MeshEditService, EdgeExtrudeInPlaneKeepsFacingLikeFloor)
{
    // Pull the +X boundary edge outward, in the floor plane. The new piece is
    // coplanar and must face the same way as the floor (+Y), as it does today.
    const BrushMesh floor = BrushOps::MakePlane({ 1.0f, 1.0f, 1.0f }, /*depthAxis*/ 1);
    MeshEditService service;

    const std::vector<EdgeElement> edges = MeshElements::Edges(floor, Transform3f::Identity());
    std::uint32_t plusX = 0;
    for (const EdgeElement& edge : edges)
        if (edge.Mid.X > 0.9f)
            plusX = edge.Index;
    const std::vector<SelectableRef> refs = {
        SelectableRef::EdgeSelection(RegistryId::Global(), kEntity, plusX),
    };

    const auto after = service.ExtrudeElements(
        floor, Transform3f::Identity(), refs, MeshElementKind::Edge, Vec3d(1.0f, 0.0f, 0.0f), true);
    ASSERT_TRUE(after.has_value());

    // Every coplanar piece still faces +Y (no inward-flipped extension).
    for (const BrushFace& face : after->Mesh.Faces)
        EXPECT_GT(BrushComputeFaceNormal(after->Mesh, face).Y, 0.9f);
}

TEST(MeshEditService, EdgeExtrudeUpContinuesFloorSurface)
{
    // The reported bug: pulling edges up made walls face the opposite way to the
    // floor's surface. Each wall must continue the floor's orientation (shared edge
    // opposed), the same rule that makes the in-plane extrude correct.
    const BrushMesh floor = BrushOps::MakePlane({ 1.0f, 1.0f, 1.0f }, /*depthAxis*/ 1);
    MeshEditService service;

    const std::vector<EdgeElement> edges = MeshElements::Edges(floor, Transform3f::Identity());
    std::vector<SelectableRef> refs;
    for (const EdgeElement& edge : edges)
        refs.push_back(SelectableRef::EdgeSelection(RegistryId::Global(), kEntity, edge.Index));

    const auto after = service.ExtrudeElements(
        floor, Transform3f::Identity(), refs, MeshElementKind::Edge, Vec3d(0.0f, 1.0f, 0.0f), true);
    ASSERT_TRUE(after.has_value());

    const std::uint32_t floorFace = FloorFace(after->Mesh);
    int walls = 0;
    for (std::uint32_t i = 0; i < after->Mesh.Faces.size(); ++i)
    {
        if (std::abs(BrushComputeFaceNormal(after->Mesh, after->Mesh.Faces[i]).Y) > 0.5f)
            continue; // a horizontal cap, not a wall
        ++walls;
        EXPECT_TRUE(FacesContinueSurface(after->Mesh, floorFace, i));
    }
    EXPECT_EQ(walls, 4);
}
