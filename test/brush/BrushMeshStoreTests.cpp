#include "brush/BrushMeshStore.h"
#include "brush/BrushOps.h"

#include <gtest/gtest.h>

TEST(BrushMeshStore, CreateAssignsUniqueValidIds)
{
    BrushMeshStore store;
    const BrushId a = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    const BrushId b = store.Create(BrushOps::MakeBox({ 2, 2, 2 }));
    EXPECT_TRUE(a.IsValid());
    EXPECT_TRUE(b.IsValid());
    EXPECT_NE(a, b);
    EXPECT_EQ(store.Count(), 2u);
    ASSERT_NE(store.Find(a), nullptr);
    EXPECT_EQ(store.Find(a)->Vertices.size(), 8u);
}

TEST(BrushMeshStore, SetPreservesIdAndKeepsCreateUnique)
{
    BrushMeshStore store;
    const BrushId loaded{ 42 };
    store.Set(loaded, BrushOps::MakeBox({ 1, 1, 1 }));
    ASSERT_NE(store.Find(loaded), nullptr);

    // A subsequent Create must not collide with the loaded id.
    const BrushId created = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    EXPECT_NE(created, loaded);
    EXPECT_GT(created.Value, loaded.Value);
}

TEST(BrushMeshStore, DestroyAndClear)
{
    BrushMeshStore store;
    const BrushId a = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    store.Destroy(a);
    EXPECT_EQ(store.Find(a), nullptr);
    EXPECT_EQ(store.Count(), 0u);

    (void)store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    store.Clear();
    EXPECT_EQ(store.Count(), 0u);
}

//=============================================================================
// Records, revisions, and the evaluation cache.
//=============================================================================

namespace
{
    BrushModifierStack OneArray(int count)
    {
        BrushModifier m;
        ArrayModifier array;
        array.Count = count;
        m.Params = array;
        return { m };
    }
}

TEST(BrushMeshStore, EveryMutationBumpsTheRevision)
{
    BrushMeshStore store;
    const BrushId id = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    const std::uint64_t r0 = store.FindRecord(id)->Revision;
    store.SetModifiers(id, OneArray(3));
    const std::uint64_t r1 = store.FindRecord(id)->Revision;
    store.Set(id, BrushOps::MakeBox({ 2, 2, 2 }));
    const std::uint64_t r2 = store.FindRecord(id)->Revision;
    EXPECT_LT(r0, r1);
    EXPECT_LT(r1, r2);
    // Set with a mesh keeps the stack the id already had.
    EXPECT_EQ(store.FindModifiers(id)->size(), 1u);
}

TEST(BrushMeshStore, EvaluationIsCachedUntilTheRecordOrPolicyChanges)
{
    BrushMeshStore store;
    const BrushId id = store.Create(BrushOps::MakeBox({ 1, 1, 1 }), OneArray(3));
    const BrushEvaluationPolicy cook = BrushEvaluationPolicy::Cook();
    const BrushEvaluated* a = store.Evaluated(id, cook);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(a->Pieces.size(), 3u);
    EXPECT_EQ(store.Evaluated(id, cook), a); // same object: no re-evaluation

    store.SetModifiers(id, OneArray(5));
    EXPECT_EQ(store.Evaluated(id, cook)->Pieces.size(), 5u);

    const BrushEvaluated* limited = store.Evaluated(id, BrushEvaluationPolicy::Interactive(2));
    EXPECT_EQ(limited->Status, BrushEvaluationStatus::PieceBudgetExceeded);
    // Asking under the cook policy again re-evaluates rather than reusing the
    // preview-limited result.
    EXPECT_EQ(store.Evaluated(id, cook)->Status, BrushEvaluationStatus::Ok);
    EXPECT_EQ(store.Evaluated(id, cook)->Pieces.size(), 5u);
}

TEST(BrushMeshStore, EvaluatedPiecesAliasTheStoredMesh)
{
    BrushMeshStore store;
    const BrushId id = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    const BrushEvaluated* e = store.Evaluated(id, BrushEvaluationPolicy::Cook());
    ASSERT_EQ(e->Pieces.size(), 1u);
    EXPECT_EQ(e->Pieces[0].Mesh, store.Find(id));
}

TEST(BrushMeshStore, DestroyDropsTheEvaluation)
{
    BrushMeshStore store;
    const BrushId id = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    (void)store.Evaluated(id, BrushEvaluationPolicy::Cook());
    store.Destroy(id);
    EXPECT_EQ(store.Evaluated(id, BrushEvaluationPolicy::Cook()), nullptr);
}

TEST(BrushMeshStoreRevisions, DomainsAdvanceOnlyForWhatChanged)
{
    BrushMeshStore store;
    const BrushId id = store.Create(BrushOps::MakeBox({ 1, 1, 1 }));
    const BrushRecord first = *store.FindRecord(id);
    EXPECT_EQ(first.TopologyRevision, first.Revision);

    // Material-only edit: material moves, topology and placement stay.
    BrushMesh painted = first.Mesh;
    painted.Faces[0].Material.Material.Path = "asset://materials/other.smat";
    store.Set(id, painted);
    const BrushRecord second = *store.FindRecord(id);
    EXPECT_GT(second.Revision, first.Revision);
    EXPECT_EQ(second.TopologyRevision, first.TopologyRevision);
    EXPECT_EQ(second.PlacementRevision, first.PlacementRevision);
    EXPECT_GT(second.MaterialRevision, first.MaterialRevision);

    // Array count: placement moves, topology and material stay.
    BrushModifier array;
    ArrayModifier params;
    params.Count = 5;
    array.Params = params;
    store.SetModifiers(id, { array });
    const BrushRecord third = *store.FindRecord(id);
    EXPECT_EQ(third.TopologyRevision, second.TopologyRevision);
    EXPECT_GT(third.PlacementRevision, second.PlacementRevision);
    EXPECT_EQ(third.MaterialRevision, second.MaterialRevision);
    params.Count = 9;
    array.Params = params;
    store.SetModifiers(id, { array });
    const BrushRecord fourth = *store.FindRecord(id);
    EXPECT_EQ(fourth.TopologyRevision, third.TopologyRevision);
    EXPECT_GT(fourth.PlacementRevision, third.PlacementRevision);

    // A mirror mints meshes: topology and placement move, material stays.
    BrushModifier mirror;
    mirror.Params = MirrorModifier{};
    store.SetModifiers(id, { mirror, array });
    const BrushRecord fifth = *store.FindRecord(id);
    EXPECT_GT(fifth.TopologyRevision, fourth.TopologyRevision);
    EXPECT_GT(fifth.PlacementRevision, fourth.PlacementRevision);
    EXPECT_EQ(fifth.MaterialRevision, fourth.MaterialRevision);

    // A vertex move: topology and placement move (bounds-relative placement
    // follows geometry), material stays.
    BrushMesh moved = fifth.Mesh;
    moved.Vertices[0].Position.Y += 0.5f;
    store.Set(id, moved);
    const BrushRecord sixth = *store.FindRecord(id);
    EXPECT_GT(sixth.TopologyRevision, fifth.TopologyRevision);
    EXPECT_GT(sixth.PlacementRevision, fifth.PlacementRevision);
    EXPECT_EQ(sixth.MaterialRevision, fifth.MaterialRevision);

    // Re-seating identical content: the sledgehammer moves, no domain does.
    store.Set(id, sixth.Mesh);
    const BrushRecord seventh = *store.FindRecord(id);
    EXPECT_GT(seventh.Revision, sixth.Revision);
    EXPECT_EQ(seventh.TopologyRevision, sixth.TopologyRevision);
    EXPECT_EQ(seventh.PlacementRevision, sixth.PlacementRevision);
    EXPECT_EQ(seventh.MaterialRevision, sixth.MaterialRevision);
}
