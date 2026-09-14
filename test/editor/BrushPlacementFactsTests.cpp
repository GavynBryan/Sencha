// The scene's retained placement facts: exact in the same frame as the edit,
// rebuilt only for the entity that changed, elements kept across placement-only
// edits, and the work counters that make a regression visible.

#include "WorkspaceFixture.h"

#include "brush/BrushBounds.h"
#include "brush/BrushWorkCounters.h"
#include "brush/MirrorModifier.h"
#include "document/BrushPlacementFacts.h"
#include "render/BrushBakeCache.h"
#include "render/BrushDrawSet.h"
#include "render/BrushFaceHighlight.h"

#include <assets/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

namespace
{
class BrushPlacementFactsTest : public WorkspaceTest
{
protected:
    [[nodiscard]] EntityId ArrayBrush(Vec3d position, int count)
    {
        const EntityId brush = AddBrush(position, { 1, 1, 1 });
        BrushModifier m;
        ArrayModifier array;
        array.Count = count;
        array.Spacing = 1.0f;
        m.Params = array;
        Scene().SetBrushModifiers(brush, { m });
        return brush;
    }

    [[nodiscard]] EntityId TortureBrush(Vec3d position)
    {
        // Twenty entries: ten mirrors and ten arrays, well under the budget.
        const EntityId brush = AddBrush(position, { 1, 1, 1 });
        BrushModifierStack stack;
        for (int i = 0; i < 10; ++i)
        {
            BrushModifier mirror;
            MirrorModifier mp;
            mp.Offset = 2.0f + static_cast<float>(i);
            mirror.Params = mp;
            mirror.Enabled = i < 2; // 4 pieces of mirrors, the rest disabled entries
            stack.push_back(mirror);
            BrushModifier array;
            ArrayModifier ap;
            ap.Count = i < 2 ? 3 : 1;
            array.Params = ap;
            stack.push_back(array);
        }
        Scene().SetBrushModifiers(brush, stack);
        return brush;
    }

    const BrushPlacementFacts& Facts() { return Scene().PlacementFacts(); }
};
}

TEST_F(BrushPlacementFactsTest, BoundsAreExactInTheSameFrameAsTheEdit)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 3);
    Scene().RefreshDerivedTransforms();
    const Aabb3d before = *Facts().GetEntityBounds(brush);
    Transform3f moved = *Scene().TryGetLocalTransform(brush);
    moved.Position.Y += 4.0f;
    Scene().SetTransform(brush, moved);
    Scene().RefreshDerivedTransforms();
    // No frame boundary between the edit and the query.
    const Aabb3d after = *Facts().GetEntityBounds(brush);
    EXPECT_NEAR(after.Min.Y - before.Min.Y, 4.0f, 1e-5f);
    EXPECT_NEAR(after.Max.X, before.Max.X, 1e-5f);
}

TEST_F(BrushPlacementFactsTest, PlacementsAndBoundsMatchTheEvaluation)
{
    const EntityId brush = AddBrush({ 1, 2, 3 }, { 2, 1, 1 });
    BrushModifier mirror;
    MirrorModifier mp;
    mp.Offset = 5.0f;
    mirror.Params = mp;
    BrushModifier array;
    ArrayModifier ap;
    ap.Count = 4;
    ap.Axis = LocalAxis::Z;
    array.Params = ap;
    Scene().SetBrushModifiers(brush, { mirror, array });
    Transform3f turned = *Scene().TryGetLocalTransform(brush);
    turned.Rotation = Quat<float>::FromAxisAngle({ 0, 1, 0 }, 0.5f);
    Scene().SetTransform(brush, turned);
    Scene().RefreshDerivedTransforms();

    const BrushEvaluated* evaluated = Facts().GetEvaluation(brush);
    const Transform3f& world = *Scene().TryGetWorldTransform(brush);
    ASSERT_NE(evaluated, nullptr);
    const auto placements = Facts().GetPiecePlacements(brush);
    const auto bounds = Facts().GetPieceBounds(brush);
    ASSERT_EQ(placements.size(), evaluated->Pieces.size());
    ASSERT_EQ(bounds.size(), evaluated->Pieces.size());
    Aabb3d expectedUnion = Aabb3d::Empty();
    for (const BrushPiece& piece : evaluated->Pieces)
    {
        EXPECT_TRUE(placements[piece.Ordinal].NearlyEquals(PieceWorldTransform(world, piece), 1e-5f));
        const Aabb3d tight = BrushWorldBounds(*piece.Mesh, PieceWorldTransform(world, piece));
        EXPECT_NEAR(bounds[piece.Ordinal].Min.X, tight.Min.X, 1e-4f);
        EXPECT_NEAR(bounds[piece.Ordinal].Max.Z, tight.Max.Z, 1e-4f);
        expectedUnion.ExpandToInclude(tight);
    }
    EXPECT_NEAR(Facts().GetEntityBounds(brush)->Min.X, expectedUnion.Min.X, 1e-4f);
    EXPECT_NEAR(Facts().GetEntityBounds(brush)->Max.Y, expectedUnion.Max.Y, 1e-4f);
    // The source bounds are the source piece's, whichever ordinal it holds.
    const Aabb3d source = bounds[evaluated->SourcePiece];
    EXPECT_NEAR(Facts().GetSourceBounds(brush)->Min.Z, source.Min.Z, 1e-5f);
    EXPECT_EQ(evaluated->Pieces[evaluated->SourcePiece].Origin, BrushPieceOrigin::Source);
}

TEST_F(BrushPlacementFactsTest, ElementsSurviveAPlacementOnlyEditAndFollowTopology)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 3);
    Scene().RefreshDerivedTransforms();
    const SourceWorldElements* elements = Facts().GetSourceWorldElements(brush);
    ASSERT_NE(elements, nullptr);
    EXPECT_EQ(elements->Edges.size(), 12u);
    EXPECT_EQ(elements->Vertices.size(), 8u);
    EXPECT_EQ(elements->Faces.size(), 6u);
    const EdgeElement* edgeData = elements->Edges.data();

    // Count edit: placements rebuild, elements stay.
    BrushModifier m = (*Scene().TryGetBrushModifiers(brush))[0];
    std::get<ArrayModifier>(m.Params).Count = 9;
    Scene().SetBrushModifiers(brush, { m });
    Scene().RefreshDerivedTransforms();
    EXPECT_EQ(Facts().GetPiecePlacements(brush).size(), 9u);
    EXPECT_EQ(Facts().GetSourceWorldElements(brush)->Edges.data(), edgeData);

    // Vertex edit: topology moved, elements rebuild (the allocator may hand
    // back the same address, so the counter is the probe, not the pointer).
    BrushMesh mesh = *Scene().TryGetBrushMesh(brush);
    mesh.Vertices[0].Position.Y += 0.5f;
    Scene().SetBrushMesh(brush, mesh);
    Scene().RefreshDerivedTransforms();
    (void)BrushWorkCounters::TakeFrame();
    EXPECT_NEAR(Facts().GetSourceWorldElements(brush)->Vertices[0].Position.Y, mesh.Vertices[0].Position.Y, 1e-5f);
    EXPECT_EQ(BrushWorkCounters::TakeFrame().ElementBuilds, 3u);

    // The edge index table answers through the source mesh's pair order.
    const auto& pair = Facts().GetEvaluation(brush)->Meshes[0].EdgePairs[5];
    EXPECT_EQ(Facts().SourceEdgeIndexOf(brush, pair[1], pair[0]), std::optional<std::uint32_t>{ 5 });
}

TEST_F(BrushPlacementFactsTest, HiddenEntitiesKeepTheirRecord)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 3);
    Scene().RefreshDerivedTransforms();
    (void)Facts().GetEntityBounds(brush);
    const std::size_t rebuilds = Facts().RebuildCount();
    Scene().SetEntityVisible(brush, false);
    Scene().RefreshDerivedTransforms();
    (void)Facts().GetEntityBounds(brush);
    Scene().SetEntityVisible(brush, true);
    Scene().RefreshDerivedTransforms();
    (void)Facts().GetEntityBounds(brush);
    EXPECT_EQ(Facts().RebuildCount(), rebuilds);
}

TEST_F(BrushPlacementFactsTest, IdleTortureSceneReconstructsNothing)
{
    const EntityId a = TortureBrush({ 0, 0, 0 });
    const EntityId b = TortureBrush({ 0, 0, 100 });
    std::uint32_t nextHandle = 1;
    BrushBakeCache bakes(BrushBakeCache::Gpu{
        .Bake = [&](const MeshGeometry&) { return StaticMeshHandle{ nextHandle++, 1 }; },
        .Destroy = [](StaticMeshHandle) {},
        .Lease = [&](const AssetRef&) { return MaterialHandle{ nextHandle++, 1 }; },
        .Release = [](MaterialHandle) {},
    });
    BrushDrawSet draws;
    const AssetRef material{ AssetType::Material, "asset://materials/dev/gray.smat" };

    // One frame of every consumer, then a hundred more of the same.
    const auto frame = [&]
    {
        Scene().RefreshDerivedTransforms();
        (void)draws.Refresh(Scene(), bakes, material);
        (void)Facts().GetEntityBounds(a);
        (void)Facts().GetSourceWorldElements(a);
        (void)Facts().GetPieceBounds(b);
        (void)Facts().GetSourceWorldElements(b);
    };
    frame();
    (void)BrushWorkCounters::TakeFrame();
    for (int i = 0; i < 100; ++i)
        frame();
    const BrushWorkCounters idle = BrushWorkCounters::TakeFrame();
    EXPECT_EQ(idle.Evaluations, 0u);
    EXPECT_EQ(idle.PlacementRebuilds, 0u);
    EXPECT_EQ(idle.ElementBuilds, 0u);
    EXPECT_EQ(idle.DrawRecordRebuilds, 0u);
    EXPECT_EQ(idle.Bakes, 0u);
    EXPECT_EQ(idle.PieceWalks, 0u);
    EXPECT_TRUE(idle.InteractiveIdle());
}

TEST_F(BrushPlacementFactsTest, MutatingOneEntityRebuildsOnlyItsShare)
{
    std::vector<EntityId> brushes;
    for (int i = 0; i < 5; ++i)
        brushes.push_back(TortureBrush({ 0, 0, static_cast<float>(i) * 100.0f }));
    std::uint32_t nextHandle = 1;
    BrushBakeCache bakes(BrushBakeCache::Gpu{
        .Bake = [&](const MeshGeometry&) { return StaticMeshHandle{ nextHandle++, 1 }; },
        .Destroy = [](StaticMeshHandle) {},
        .Lease = [&](const AssetRef&) { return MaterialHandle{ nextHandle++, 1 }; },
        .Release = [](MaterialHandle) {},
    });
    BrushDrawSet draws;
    const AssetRef material{ AssetType::Material, "asset://materials/dev/gray.smat" };
    const auto frame = [&]
    {
        Scene().RefreshDerivedTransforms();
        (void)draws.Refresh(Scene(), bakes, material);
        for (EntityId brush : brushes)
        {
            (void)Facts().GetEntityBounds(brush);
            (void)Facts().GetSourceWorldElements(brush);
        }
    };
    frame();
    (void)BrushWorkCounters::TakeFrame();

    // Transform: one placement rebuild, one draw record, elements rebuilt for
    // the one entity (they are world-space), nothing evaluated or baked.
    Transform3f moved = *Scene().TryGetLocalTransform(brushes[2]);
    moved.Position.X += 3.0f;
    Scene().SetTransform(brushes[2], moved);
    frame();
    BrushWorkCounters after = BrushWorkCounters::TakeFrame();
    EXPECT_EQ(after.Evaluations, 0u);
    EXPECT_EQ(after.PlacementRebuilds, 1u);
    EXPECT_EQ(after.DrawRecordRebuilds, 1u);
    EXPECT_EQ(after.ElementBuilds, 3u); // edges, vertices, faces of that entity
    EXPECT_EQ(after.Bakes, 0u);

    // Count: one evaluation, one placement rebuild, one draw record, no
    // element build (topology unchanged), no bake.
    BrushModifierStack stack = *Scene().TryGetBrushModifiers(brushes[2]);
    std::get<ArrayModifier>(stack[1].Params).Count = 5;
    Scene().SetBrushModifiers(brushes[2], stack);
    frame();
    after = BrushWorkCounters::TakeFrame();
    EXPECT_EQ(after.Evaluations, 1u);
    EXPECT_EQ(after.PlacementRebuilds, 1u);
    EXPECT_EQ(after.DrawRecordRebuilds, 1u);
    EXPECT_EQ(after.ElementBuilds, 0u);
    EXPECT_EQ(after.Bakes, 0u);

    // Vertex: everything for that entity, still nothing for the other four.
    BrushMesh mesh = *Scene().TryGetBrushMesh(brushes[2]);
    mesh.Vertices[0].Position.Y += 0.25f;
    Scene().SetBrushMesh(brushes[2], mesh);
    frame();
    after = BrushWorkCounters::TakeFrame();
    EXPECT_EQ(after.Evaluations, 1u);
    EXPECT_EQ(after.PlacementRebuilds, 1u);
    EXPECT_EQ(after.DrawRecordRebuilds, 1u);
    EXPECT_EQ(after.ElementBuilds, 3u);
    EXPECT_EQ(after.Bakes, 4u); // source, each mirror of it, and the mirror of the mirror
}

TEST_F(BrushPlacementFactsTest, FaceHighlightResolvesPerSourceFaceAndDistinctMesh)
{
    const EntityId brush = AddBrush({ 0, 0, 0 }, { 1, 1, 1 });
    BrushModifier mirror;
    MirrorModifier mp;
    mp.Offset = 4.0f;
    mirror.Params = mp;
    BrushModifier array;
    ArrayModifier ap;
    ap.Count = 50;
    array.Params = ap;
    Scene().SetBrushModifiers(brush, { mirror, array });
    Scene().RefreshDerivedTransforms();
    const BrushEvaluated* evaluated = Facts().GetEvaluation(brush);
    ASSERT_NE(evaluated, nullptr);
    ASSERT_EQ(evaluated->Pieces.size(), 100u);

    const BrushFaceHighlight highlight = BuildBrushFaceHighlight(*evaluated, 3, 7);
    EXPECT_EQ(highlight.TopologyRevision, 7u);
    ASSERT_EQ(highlight.Meshes.size(), 2u); // one per distinct mesh, never per piece
    for (const BrushFaceHighlightMesh& mesh : highlight.Meshes)
    {
        EXPECT_EQ(mesh.Outline.size(), 8u); // a quad: four edges, two vertices each
        EXPECT_EQ(mesh.Fill.size(), 6u);    // two triangles
    }
}
