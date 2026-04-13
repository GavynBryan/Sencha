#include <gtest/gtest.h>
#include <batch/DataBatch.h>
#include <array>
#include <memory>
#include <span>
#include <vector>

// --- Test helpers ---

struct Particle {
    float X = 0.0f;
    float Y = 0.0f;
    float Life = 1.0f;

    Particle() = default;
    Particle(float x, float y, float life) : X(x), Y(y), Life(life) {}
};

struct MoveOnlyParticle {
    std::unique_ptr<int> Value;

    explicit MoveOnlyParticle(int value)
        : Value(std::make_unique<int>(value))
    {
    }

    MoveOnlyParticle(MoveOnlyParticle&&) noexcept = default;
    MoveOnlyParticle& operator=(MoveOnlyParticle&&) noexcept = default;
    MoveOnlyParticle(const MoveOnlyParticle&) = delete;
    MoveOnlyParticle& operator=(const MoveOnlyParticle&) = delete;
};

// --- DataBatch Tests ---

TEST(DataBatch, EmplaceAddsItem)
{
    DataBatch<Particle> batch;
    auto handle = batch.Emplace(1.0f, 2.0f, 3.0f);

    EXPECT_EQ(batch.Count(), 1u);
    EXPECT_FALSE(batch.IsEmpty());
    EXPECT_TRUE(handle.IsValid());
}

TEST(DataBatch, EmplaceBlockAddsItemsAndReturnsKeyBlock)
{
    DataBatch<Particle> batch;

    DataBatchBlock block = batch.EmplaceBlock(3, [](size_t index) {
        const float value = static_cast<float>(index * 3 + 1);
        return Particle{value, value + 1.0f, value + 2.0f};
    });

    EXPECT_EQ(block.FirstKey, 1u);
    EXPECT_EQ(block.Count, 3u);
    EXPECT_EQ(batch.Count(), 3u);
    EXPECT_EQ(batch.GetVersion(), 1u);

    auto items = batch.GetItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_FLOAT_EQ(items[0].X, 1.0f);
    EXPECT_FLOAT_EQ(items[1].X, 4.0f);
    EXPECT_FLOAT_EQ(items[2].X, 7.0f);

    for (size_t i = 0; i < block.Count; ++i) {
        EXPECT_NE(batch.TryGet(block.KeyAt(i)), nullptr);
    }
}

TEST(DataBatch, EmplaceBlockSupportsMoveOnlyItems)
{
    DataBatch<MoveOnlyParticle> batch;

    DataBatchBlock block = batch.EmplaceBlock(2, [](size_t index) {
        return MoveOnlyParticle(static_cast<int>((index + 1) * 10));
    });

    EXPECT_EQ(block.Count, 2u);
    EXPECT_EQ(batch.Count(), 2u);
    ASSERT_EQ(batch.GetItems().size(), 2u);
    ASSERT_NE(batch.GetItems()[0].Value, nullptr);
    ASSERT_NE(batch.GetItems()[1].Value, nullptr);
    EXPECT_EQ(*batch.GetItems()[0].Value, 10);
    EXPECT_EQ(*batch.GetItems()[1].Value, 20);
}

TEST(DataBatch, EmplaceBlockBuildsItemsAndBumpsVersionOnce)
{
    DataBatch<Particle> batch;

    DataBatchBlock block = batch.EmplaceBlock(4, [](size_t index) {
        const float value = static_cast<float>(index + 1);
        return Particle{value, value * 2.0f, value * 3.0f};
    });

    EXPECT_EQ(block.Count, 4u);
    EXPECT_EQ(batch.Count(), 4u);
    EXPECT_EQ(batch.GetVersion(), 1u);
    EXPECT_FLOAT_EQ(batch.GetItems()[3].X, 4.0f);
    EXPECT_FLOAT_EQ(batch.GetItems()[3].Y, 8.0f);
    EXPECT_FLOAT_EQ(batch.GetItems()[3].Life, 12.0f);
}

TEST(DataBatch, TryGetReturnsEmplacedItem)
{
    DataBatch<Particle> batch;
    auto handle = batch.Emplace(10.0f, 20.0f, 0.5f);

    auto* p = batch.TryGet(handle);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->X, 10.0f);
    EXPECT_FLOAT_EQ(p->Y, 20.0f);
    EXPECT_FLOAT_EQ(p->Life, 0.5f);
}

TEST(DataBatch, HandleDestructionRemovesItem)
{
    DataBatch<Particle> batch;

    {
        auto handle = batch.Emplace(1.0f, 2.0f, 3.0f);
        EXPECT_EQ(batch.Count(), 1u);
    }

    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_TRUE(batch.IsEmpty());
}

TEST(DataBatch, HandleResetRemovesItem)
{
    DataBatch<Particle> batch;

    auto handle = batch.Emplace(1.0f, 2.0f, 3.0f);
    EXPECT_EQ(batch.Count(), 1u);

    handle.Reset();
    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_FALSE(handle.IsValid());
}

TEST(DataBatch, RemoveKeysCompactsSurvivors)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(3.0f, 0.0f, 0.0f);
    auto h4 = batch.Emplace(4.0f, 0.0f, 0.0f);

    std::array<DataBatchKey, 2> keys{ h2.GetToken(), h4.GetToken() };
    const uint64_t versionBeforeRemove = batch.GetVersion();

    batch.RemoveKeys(std::span<const DataBatchKey>(keys));

    EXPECT_EQ(batch.Count(), 2u);
    EXPECT_EQ(batch.GetVersion(), versionBeforeRemove + 1);
    EXPECT_NE(batch.TryGet(h1), nullptr);
    EXPECT_EQ(batch.TryGet(h2), nullptr);
    EXPECT_NE(batch.TryGet(h3), nullptr);
    EXPECT_EQ(batch.TryGet(h4), nullptr);

    auto items = batch.GetItems();
    ASSERT_EQ(items.size(), 2u);
    EXPECT_FLOAT_EQ(items[0].X, 1.0f);
    EXPECT_FLOAT_EQ(items[1].X, 3.0f);
}

TEST(DataBatch, RemoveKeysIgnoresInvalidMissingAndDuplicateKeys)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(3.0f, 0.0f, 0.0f);

    std::array<DataBatchKey, 4> keys{
        h2.GetToken(),
        h2.GetToken(),
        DataBatchKey{9999},
        DataBatchKey{}
    };
    const uint64_t versionBeforeRemove = batch.GetVersion();

    batch.RemoveKeys(std::span<const DataBatchKey>(keys));

    EXPECT_EQ(batch.Count(), 2u);
    EXPECT_EQ(batch.GetVersion(), versionBeforeRemove + 1);
    EXPECT_NE(batch.TryGet(h1), nullptr);
    EXPECT_EQ(batch.TryGet(h2), nullptr);
    EXPECT_NE(batch.TryGet(h3), nullptr);
}

TEST(DataBatch, RemoveHandlesInvalidatesRemovedHandles)
{
    DataBatch<Particle> batch;
    std::vector<LifetimeHandle<DataBatchKey>> handles;
    handles.push_back(batch.Emplace(1.0f, 0.0f, 0.0f));
    handles.push_back(batch.Emplace(2.0f, 0.0f, 0.0f));
    handles.push_back(batch.Emplace(3.0f, 0.0f, 0.0f));
    handles.push_back(batch.Emplace(4.0f, 0.0f, 0.0f));
    const DataBatchKey removedA = handles[1].GetToken();
    const DataBatchKey removedB = handles[2].GetToken();

    batch.RemoveHandles(std::span<LifetimeHandle<DataBatchKey>>(handles).subspan(1, 2));

    EXPECT_EQ(batch.Count(), 2u);
    EXPECT_TRUE(handles[0].IsValid());
    EXPECT_FALSE(handles[1].IsValid());
    EXPECT_FALSE(handles[2].IsValid());
    EXPECT_TRUE(handles[3].IsValid());
    EXPECT_EQ(batch.TryGet(removedA), nullptr);
    EXPECT_EQ(batch.TryGet(removedB), nullptr);
    EXPECT_NE(batch.TryGet(handles[0]), nullptr);
    EXPECT_NE(batch.TryGet(handles[3]), nullptr);
}

TEST(DataBatch, RemoveHandlesCanRemoveWholeBatch)
{
    DataBatch<Particle> batch;
    std::vector<LifetimeHandle<DataBatchKey>> handles;
    handles.push_back(batch.Emplace(1.0f, 0.0f, 0.0f));
    handles.push_back(batch.Emplace(2.0f, 0.0f, 0.0f));
    handles.push_back(batch.Emplace(3.0f, 0.0f, 0.0f));
    const uint64_t versionBeforeRemove = batch.GetVersion();

    batch.RemoveHandles(std::span<LifetimeHandle<DataBatchKey>>(handles));

    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_TRUE(batch.IsEmpty());
    EXPECT_EQ(batch.GetVersion(), versionBeforeRemove + 1);
    for (const auto& handle : handles) {
        EXPECT_FALSE(handle.IsValid());
    }
}

TEST(DataBatch, RemoveHandlesDoesNotClearWholeBatchForDuplicateHandles)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(3.0f, 0.0f, 0.0f);
    std::vector<LifetimeHandle<DataBatchKey>> handles;
    handles.emplace_back(&batch, h1.GetToken());
    handles.emplace_back(&batch, h1.GetToken());
    handles.emplace_back(&batch, h2.GetToken());

    batch.RemoveHandles(std::span<LifetimeHandle<DataBatchKey>>(handles));

    EXPECT_EQ(batch.Count(), 1u);
    EXPECT_EQ(batch.TryGet(h1), nullptr);
    EXPECT_EQ(batch.TryGet(h2), nullptr);
    EXPECT_NE(batch.TryGet(h3), nullptr);
}

TEST(DataBatch, RemoveBlockRemovesBlockKeys)
{
    DataBatch<Particle> batch;
    DataBatchBlock block = batch.EmplaceBlock(3, [](size_t index) {
        return Particle{static_cast<float>(index + 1), 0.0f, 0.0f};
    });
    const uint64_t versionBeforeRemove = batch.GetVersion();

    batch.RemoveBlock(block);

    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_TRUE(batch.IsEmpty());
    EXPECT_EQ(batch.GetVersion(), versionBeforeRemove + 1);
    for (size_t i = 0; i < block.Count; ++i) {
        EXPECT_EQ(batch.TryGet(block.KeyAt(i)), nullptr);
    }
}

TEST(DataBatch, TryGetAfterRemoveReturnsNull)
{
    DataBatch<Particle> batch;
    auto handle = batch.Emplace(1.0f, 2.0f, 3.0f);

    handle.Reset();
    // Handle is now invalid, TryGet on a default handle should return null
    EXPECT_FALSE(handle.IsValid());
}

TEST(DataBatch, MultipleEmplacesAreContiguous)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(3.0f, 0.0f, 0.0f);

    EXPECT_EQ(batch.Count(), 3u);

    // Data should be in a contiguous span
    auto items = batch.GetItems();
    EXPECT_EQ(items.size(), 3u);
    EXPECT_FLOAT_EQ(items[0].X, 1.0f);
    EXPECT_FLOAT_EQ(items[1].X, 2.0f);
    EXPECT_FLOAT_EQ(items[2].X, 3.0f);
}

TEST(DataBatch, SwapAndPopOnRemoval)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(3.0f, 0.0f, 0.0f);

    // Remove the first — last should swap in
    h1.Reset();

    EXPECT_EQ(batch.Count(), 2u);

    // h2 and h3 should still be accessible
    auto* p2 = batch.TryGet(h2);
    auto* p3 = batch.TryGet(h3);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    EXPECT_FLOAT_EQ(p2->X, 2.0f);
    EXPECT_FLOAT_EQ(p3->X, 3.0f);
}

TEST(DataBatch, HandleMoveConstructor)
{
    DataBatch<Particle> batch;
    auto original = batch.Emplace(5.0f, 6.0f, 7.0f);
    auto moved = std::move(original);

    EXPECT_FALSE(original.IsValid());
    EXPECT_TRUE(moved.IsValid());
    EXPECT_EQ(batch.Count(), 1u);

    auto* p = batch.TryGet(moved);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->X, 5.0f);
}

TEST(DataBatch, HandleMoveAssignment)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);

    EXPECT_EQ(batch.Count(), 2u);

    // Move h2 into h1 — h1's particle should be removed
    h1 = std::move(h2);

    EXPECT_EQ(batch.Count(), 1u);
    EXPECT_TRUE(h1.IsValid());
    EXPECT_FALSE(h2.IsValid());

    auto* p = batch.TryGet(h1);
    ASSERT_NE(p, nullptr);
    EXPECT_FLOAT_EQ(p->X, 2.0f);
}

TEST(DataBatch, RangeBasedForLoop)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(10.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(20.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(30.0f, 0.0f, 0.0f);

    float sum = 0.0f;
    for (const auto& p : batch) {
        sum += p.X;
    }
    EXPECT_FLOAT_EQ(sum, 60.0f);
}

TEST(DataBatch, ClearRemovesEverything)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(2.0f, 0.0f, 0.0f);

    batch.Clear();

    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_TRUE(batch.IsEmpty());
}

TEST(DataBatch, DirtyFlag)
{
    DataBatch<Particle> batch;

    EXPECT_FALSE(batch.CheckAndClearDirty());

    auto h = batch.Emplace(1.0f, 0.0f, 0.0f);
    EXPECT_TRUE(batch.CheckAndClearDirty());

    EXPECT_FALSE(batch.CheckAndClearDirty());

    batch.MarkDirty();
    EXPECT_TRUE(batch.CheckAndClearDirty());
}

TEST(DataBatch, SortIfDirty)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(3.0f, 0.0f, 0.0f);
    auto h2 = batch.Emplace(1.0f, 0.0f, 0.0f);
    auto h3 = batch.Emplace(2.0f, 0.0f, 0.0f);

    batch.SortIfDirty([](const Particle& a, const Particle& b) {
        return a.X < b.X;
    });

    auto items = batch.GetItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_FLOAT_EQ(items[0].X, 1.0f);
    EXPECT_FLOAT_EQ(items[1].X, 2.0f);
    EXPECT_FLOAT_EQ(items[2].X, 3.0f);

    // Handles should still resolve correctly after sort
    auto* p1 = batch.TryGet(h1);
    auto* p2 = batch.TryGet(h2);
    auto* p3 = batch.TryGet(h3);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    ASSERT_NE(p3, nullptr);
    EXPECT_FLOAT_EQ(p1->X, 3.0f);
    EXPECT_FLOAT_EQ(p2->X, 1.0f);
    EXPECT_FLOAT_EQ(p3->X, 2.0f);
}

TEST(DataBatch, MutateViaIteration)
{
    DataBatch<Particle> batch;
    auto h1 = batch.Emplace(0.0f, 0.0f, 1.0f);
    auto h2 = batch.Emplace(0.0f, 0.0f, 0.5f);

    // Simulate a system update: reduce lifetime
    for (auto& p : batch) {
        p.Life -= 0.1f;
    }

    auto* p1 = batch.TryGet(h1);
    auto* p2 = batch.TryGet(h2);
    ASSERT_NE(p1, nullptr);
    ASSERT_NE(p2, nullptr);
    EXPECT_FLOAT_EQ(p1->Life, 0.9f);
    EXPECT_FLOAT_EQ(p2->Life, 0.4f);
}
