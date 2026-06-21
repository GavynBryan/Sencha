#include "meshedit/MeshEditService.h"

#include "meshedit/MeshElements.h"
#include "level/brush/BrushOps.h"
#include "level/brush/BrushValidation.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <initializer_list>
#include <optional>
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

TEST(MeshEditService, SplitEdgeInsertsMidpointAndStaysClosed)
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
        service.ApplyVerb(target, selection, MeshEditVerb::SplitEdge);

    ASSERT_NE(command, nullptr);
    const auto* captured = dynamic_cast<const CapturingCommand*>(command.get());
    ASSERT_NE(captured, nullptr);
    EXPECT_EQ(captured->After.Vertices.size(), box.Vertices.size() + 1); // one midpoint
    EXPECT_EQ(captured->After.Faces.size(), box.Faces.size());           // split adds no faces

    BrushMesh check = captured->After;
    const BrushRepairResult repair = BrushValidateAndRepair(check);
    EXPECT_TRUE(repair.Ok);
    EXPECT_TRUE(repair.Closed); // sub-edges remain shared by exactly two faces
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
