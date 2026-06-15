#include "level/brush/BrushMeshStore.h"
#include "level/brush/BrushOps.h"

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
