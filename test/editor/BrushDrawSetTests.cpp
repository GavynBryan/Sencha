// Retention rules of the brush draw set: an unchanged frame recomputes no
// geometry, a change rebuilds only the entity that changed, and what is
// retained is exactly what the evaluator and transforms say.

#include "WorkspaceFixture.h"

#include "brush/BrushBounds.h"
#include "brush/MirrorModifier.h"
#include "meshedit/MeshElements.h"
#include "render/BrushDrawSet.h"

#include <assets/static_mesh/MeshGeometry.h>

#include <gtest/gtest.h>

namespace
{
class BrushDrawSetTest : public WorkspaceTest
{
protected:
    BrushDrawSetTest()
        : Bakes(BrushBakeCache::Gpu{
              .Bake = [this](const MeshGeometry&) { return StaticMeshHandle{ NextMesh++, 1 }; },
              .Destroy = [](StaticMeshHandle) {},
              .Lease = [this](const AssetRef&) { return MaterialHandle{ NextMaterial++, 1 }; },
              .Release = [](MaterialHandle) {},
          })
    {
    }

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

    bool Refresh()
    {
        Scene().RefreshDerivedTransforms();
        return Draws.Refresh(Scene(), Bakes, Default);
    }

    const AssetRef Default{ AssetType::Material, "asset://materials/dev/gray.smat" };
    std::uint32_t NextMesh = 1;
    std::uint32_t NextMaterial = 1;
    BrushDrawSet Draws;
    BrushBakeCache Bakes;
};
}

TEST_F(BrushDrawSetTest, UnchangedFramesRebuildNothing)
{
    (void)ArrayBrush({ 0, 0, 0 }, 20);
    EXPECT_TRUE(Refresh());
    const std::uint64_t version = Draws.Version();
    EXPECT_EQ(Draws.EntityRebuildCount(), 1u);
    for (int i = 0; i < 100; ++i)
        EXPECT_FALSE(Refresh());
    EXPECT_EQ(Draws.Version(), version);
    EXPECT_EQ(Draws.EntityRebuildCount(), 1u);
}

TEST_F(BrushDrawSetTest, MovingOneEntityRebuildsOnlyThatRecord)
{
    const EntityId a = ArrayBrush({ 0, 0, 0 }, 10);
    const EntityId b = ArrayBrush({ 0, 0, 40 }, 10);
    ASSERT_TRUE(Refresh());
    ASSERT_EQ(Draws.EntityRebuildCount(), 2u);
    const BrushPlacement* bBefore = Draws.Find(b)->Placements.data();
    const std::uint64_t bDigest = Draws.Find(b)->Digest;
    const std::uint64_t version = Draws.Version();

    Transform3f moved = *Scene().TryGetLocalTransform(a);
    moved.Position.Y += 5.0f;
    Scene().SetTransform(a, moved);
    ASSERT_TRUE(Refresh());
    EXPECT_EQ(Draws.EntityRebuildCount(), 3u);
    EXPECT_EQ(Draws.Version(), version + 1);
    EXPECT_EQ(Draws.Find(b)->Placements.data(), bBefore);
    EXPECT_EQ(Draws.Find(b)->Digest, bDigest);
    EXPECT_NEAR(Draws.Find(a)->Placements[0].World.Data[1][3], 5.0f, 1e-5f);
}

TEST_F(BrushDrawSetTest, EachMutationRebuildsOnceAndLockDoesNot)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 5);
    ASSERT_TRUE(Refresh());
    std::size_t rebuilds = Draws.EntityRebuildCount();

    BrushModifier m;
    ArrayModifier array;
    array.Count = 7;
    m.Params = array;
    Scene().SetBrushModifiers(brush, { m }); // record revision
    EXPECT_TRUE(Refresh());
    EXPECT_EQ(Draws.EntityRebuildCount(), ++rebuilds);
    EXPECT_EQ(Draws.Find(brush)->Placements.size(), 7u);

    Scene().SetEntityLocked(brush, true); // locking is not a draw property
    EXPECT_FALSE(Refresh());
    EXPECT_EQ(Draws.EntityRebuildCount(), rebuilds);

    Scene().SetEntityVisible(brush, false);
    EXPECT_TRUE(Refresh());
    EXPECT_EQ(Draws.Find(brush), nullptr);
    Scene().SetEntityVisible(brush, true);
    EXPECT_TRUE(Refresh());
    EXPECT_EQ(Draws.EntityRebuildCount(), ++rebuilds);

    Scene().SetInteractiveEvaluationPolicy(BrushEvaluationPolicy::Interactive(3));
    EXPECT_TRUE(Refresh());
    EXPECT_EQ(Draws.EntityRebuildCount(), ++rebuilds);
    EXPECT_EQ(Draws.Find(brush)->Placements.size(), 1u); // the array was refused
    Scene().SetInteractiveEvaluationPolicy(BrushEvaluationPolicy::Interactive(4096));
    EXPECT_TRUE(Refresh());
    ++rebuilds;

    // A default-material change re-bakes: placements redo, edge topology stays.
    const std::vector<BrushEdgeVertex>* edges = &Draws.Find(brush)->Meshes[0].Edges;
    const BrushEdgeVertex* edgeData = edges->data();
    const AssetRef other{ AssetType::Material, "asset://materials/dev/blue.smat" };
    Scene().RefreshDerivedTransforms();
    EXPECT_TRUE(Draws.Refresh(Scene(), Bakes, other));
    EXPECT_EQ(Draws.EntityRebuildCount(), ++rebuilds);
    EXPECT_EQ(Draws.Find(brush)->Meshes[0].Edges.data(), edgeData);

    const EntityId second = ArrayBrush({ 0, 0, 30 }, 2);
    EXPECT_TRUE(Draws.Refresh(Scene(), Bakes, other));
    EXPECT_EQ(Draws.EntityRebuildCount(), ++rebuilds);
    Scene().DestroyEntity(second);
    EXPECT_TRUE(Draws.Refresh(Scene(), Bakes, other));
    EXPECT_EQ(Draws.EntityRebuildCount(), rebuilds);
    EXPECT_EQ(Draws.Entities().size(), 1u);
}

TEST_F(BrushDrawSetTest, PlacementsMatchTheEvaluationExactly)
{
    const EntityId brush = AddBrush({ 3, 2, 1 }, { 2, 1, 1 });
    BrushModifier mirror;
    MirrorModifier mp;
    mp.Offset = 4.0f;
    mirror.Params = mp;
    BrushModifier array;
    ArrayModifier ap;
    ap.Axis = LocalAxis::Z;
    ap.Count = 3;
    ap.Spacing = 0.5f;
    array.Params = ap;
    Scene().SetBrushModifiers(brush, { mirror, array });
    Transform3f turned = *Scene().TryGetLocalTransform(brush);
    turned.Rotation = Quat<float>::FromAxisAngle({ 0, 1, 0 }, 0.7f);
    turned.Scale = { 2, 1, 0.5f };
    Scene().SetTransform(brush, turned);
    ASSERT_TRUE(Refresh());

    const BrushDrawEntity* record = Draws.Find(brush);
    ASSERT_NE(record, nullptr);
    const BrushEvaluated* evaluated = Scene().TryGetBrushPieces(brush);
    const Transform3f& world = *Scene().TryGetWorldTransform(brush);
    ASSERT_EQ(record->Placements.size(), evaluated->Pieces.size());
    ASSERT_EQ(record->Runs.size(), 2u);
    ASSERT_EQ(record->Meshes.size(), 2u);
    EXPECT_TRUE(record->Meshes[0].Handle.IsValid());
    EXPECT_EQ(record->Meshes[0].SlotMaterials.size(), 1u);

    // Runs are contiguous per mesh and every placement is the piece's own transform.
    std::size_t matched = 0;
    for (const BrushMeshRun& run : record->Runs)
    {
        std::vector<const BrushPiece*> pieces;
        for (const BrushPiece& piece : evaluated->Pieces)
            if (piece.MeshIndex == run.MeshIndex)
                pieces.push_back(&piece);
        ASSERT_EQ(pieces.size(), run.PlacementCount);
        for (std::uint32_t i = 0; i < run.PlacementCount; ++i)
        {
            const BrushPlacement& placement = record->Placements[run.FirstPlacement + i];
            const Mat4 expected = PieceWorldTransform(world, *pieces[i]).ToMat4();
            for (int r = 0; r < 4; ++r)
                for (int c = 0; c < 4; ++c)
                    EXPECT_NEAR(placement.World.Data[r][c], expected.Data[r][c], 1e-4f);
            EXPECT_EQ(record->InstanceRows[run.FirstPlacement + i], placement.World);
            const Aabb3d tight = BrushWorldBounds(*pieces[i]->Mesh, PieceWorldTransform(world, *pieces[i]));
            EXPECT_NEAR(placement.WorldBounds.Min.X, tight.Min.X, 1e-4f);
            EXPECT_NEAR(placement.WorldBounds.Max.Z, tight.Max.Z, 1e-4f);
            ++matched;
        }
    }
    EXPECT_EQ(matched, evaluated->Pieces.size());

    for (std::size_t m = 0; m < record->Meshes.size(); ++m)
        EXPECT_EQ(record->Meshes[m].Edges.size(),
                  MeshElements::UniqueEdgeVertexPairs(*evaluated->Meshes[m].Mesh).size() * 2);
}

TEST_F(BrushDrawSetTest, DigestFollowsContentAndPlacementAndReturnsAfterUndo)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 4);
    ASSERT_TRUE(Refresh());
    const std::uint64_t original = Draws.ContentDigest();
    const Transform3f home = *Scene().TryGetLocalTransform(brush);

    Transform3f moved = home;
    moved.Position.X += 1.0f;
    Scene().SetTransform(brush, moved);
    ASSERT_TRUE(Refresh());
    EXPECT_NE(Draws.ContentDigest(), original);
    Scene().SetTransform(brush, home);
    ASSERT_TRUE(Refresh());
    EXPECT_EQ(Draws.ContentDigest(), original);

    const BrushModifierStack stack = *Scene().TryGetBrushModifiers(brush);
    BrushModifier more = stack[0];
    std::get<ArrayModifier>(more.Params).Count = 9;
    Scene().SetBrushModifiers(brush, { more });
    ASSERT_TRUE(Refresh());
    EXPECT_NE(Draws.ContentDigest(), original);
    Scene().SetBrushModifiers(brush, stack);
    ASSERT_TRUE(Refresh());
    EXPECT_EQ(Draws.ContentDigest(), original);
}

TEST_F(BrushDrawSetTest, PlacementKeyChangesOnlyWithPlacementFacts)
{
    const EntityId brush = ArrayBrush({ 0, 0, 0 }, 2);
    Scene().RefreshDerivedTransforms();
    const auto first = MakeBrushPlacementKey(Scene(), brush);
    ASSERT_TRUE(first.has_value());
    Scene().RefreshDerivedTransforms();
    EXPECT_EQ(*MakeBrushPlacementKey(Scene(), brush), *first);
    Scene().SetEntityLocked(brush, true);
    EXPECT_EQ(*MakeBrushPlacementKey(Scene(), brush), *first);
    Transform3f moved = *Scene().TryGetLocalTransform(brush);
    moved.Position.Z += 1.0f;
    Scene().SetTransform(brush, moved);
    Scene().RefreshDerivedTransforms();
    EXPECT_NE(*MakeBrushPlacementKey(Scene(), brush), *first);
}
