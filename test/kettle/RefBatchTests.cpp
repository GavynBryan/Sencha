#include <gtest/gtest.h>
#include <batch/RefBatch.h>
#include <raii/RefBatchHandle.h>

// --- Test helpers ---

struct TestItem {
    int Value = 0;
};

// --- RefBatch Tests ---

TEST(RefBatch, AddAndContains)
{
    RefBatch<TestItem> batch;
    TestItem item;

    batch.Add(&item);
    EXPECT_TRUE(batch.Contains(&item));
    EXPECT_EQ(batch.Count(), 1u);
}

TEST(RefBatch, AddDuplicateIsIgnored)
{
    RefBatch<TestItem> batch;
    TestItem item;

    batch.Add(&item);
    batch.Add(&item);
    EXPECT_EQ(batch.Count(), 1u);
}

TEST(RefBatch, RemoveItem)
{
    RefBatch<TestItem> batch;
    TestItem item;

    batch.Add(&item);
    batch.Remove(&item);

    EXPECT_FALSE(batch.Contains(&item));
    EXPECT_EQ(batch.Count(), 0u);
}

TEST(RefBatch, RemoveNonExistentIsNoOp)
{
    RefBatch<TestItem> batch;
    TestItem item;

    batch.Remove(&item);
    EXPECT_EQ(batch.Count(), 0u);
}

TEST(RefBatch, SwapAndPopPreservesOtherItems)
{
    RefBatch<TestItem> batch;
    TestItem a{1}, b{2}, c{3};

    batch.Add(&a);
    batch.Add(&b);
    batch.Add(&c);

    batch.Remove(&a);

    EXPECT_EQ(batch.Count(), 2u);
    EXPECT_FALSE(batch.Contains(&a));
    EXPECT_TRUE(batch.Contains(&b));
    EXPECT_TRUE(batch.Contains(&c));
}

TEST(RefBatch, GetItemsReturnsAllItems)
{
    RefBatch<TestItem> batch;
    TestItem a{1}, b{2};

    batch.Add(&a);
    batch.Add(&b);

    auto items = batch.GetItems();
    EXPECT_EQ(items.size(), 2u);
}

TEST(RefBatch, IsEmptyReportsCorrectly)
{
    RefBatch<TestItem> batch;
    EXPECT_TRUE(batch.IsEmpty());

    TestItem item;
    batch.Add(&item);
    EXPECT_FALSE(batch.IsEmpty());
}

TEST(RefBatch, ClearRemovesEverything)
{
    RefBatch<TestItem> batch;
    TestItem a, b;

    batch.Add(&a);
    batch.Add(&b);
    batch.Clear();

    EXPECT_EQ(batch.Count(), 0u);
    EXPECT_TRUE(batch.IsEmpty());
    EXPECT_FALSE(batch.Contains(&a));
}

TEST(RefBatch, DirtyFlag)
{
    RefBatch<TestItem> batch;
    TestItem item;

    EXPECT_FALSE(batch.CheckAndClearDirty());

    batch.Add(&item);
    EXPECT_TRUE(batch.CheckAndClearDirty());

    EXPECT_FALSE(batch.CheckAndClearDirty());

    batch.MarkDirty();
    EXPECT_TRUE(batch.CheckAndClearDirty());
}

TEST(RefBatch, SortIfDirty)
{
    RefBatch<TestItem> batch;
    TestItem a{3}, b{1}, c{2};

    batch.Add(&a);
    batch.Add(&b);
    batch.Add(&c);

    batch.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    auto items = batch.GetItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0]->Value, 1);
    EXPECT_EQ(items[1]->Value, 2);
    EXPECT_EQ(items[2]->Value, 3);
}

TEST(RefBatch, SortIfDirtySkipsWhenClean)
{
    RefBatch<TestItem> batch;
    TestItem a{2}, b{1};

    batch.Add(&a);
    batch.Add(&b);

    batch.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    EXPECT_EQ(batch.GetItems()[0]->Value, 1);

    batch.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value > rhs->Value;
    });

    EXPECT_EQ(batch.GetItems()[0]->Value, 1);
}

TEST(RefBatch, RangeBasedForLoop)
{
    RefBatch<TestItem> batch;
    TestItem a{10}, b{20};

    batch.Add(&a);
    batch.Add(&b);

    int sum = 0;
    for (const auto* item : batch) {
        sum += item->Value;
    }
    EXPECT_EQ(sum, 30);
}

// --- RefBatchHandle Tests ---

TEST(RefBatchHandle, AddsOnConstructionRemovesOnDestruction)
{
    RefBatch<TestItem> batch;
    TestItem item;

    {
        RefBatchHandle<TestItem> handle(&batch, &item);
        EXPECT_TRUE(batch.Contains(&item));
        EXPECT_EQ(batch.Count(), 1u);
    }

    EXPECT_FALSE(batch.Contains(&item));
    EXPECT_EQ(batch.Count(), 0u);
}

TEST(RefBatchHandle, ResetRemovesEarly)
{
    RefBatch<TestItem> batch;
    TestItem item;

    RefBatchHandle<TestItem> handle(&batch, &item);
    EXPECT_TRUE(batch.Contains(&item));

    handle.Reset();
    EXPECT_FALSE(batch.Contains(&item));
    EXPECT_FALSE(handle.IsValid());
}

TEST(RefBatchHandle, MoveConstructor)
{
    RefBatch<TestItem> batch;
    TestItem item;

    RefBatchHandle<TestItem> original(&batch, &item);
    RefBatchHandle<TestItem> moved(std::move(original));

    EXPECT_FALSE(original.IsValid());
    EXPECT_TRUE(moved.IsValid());
    EXPECT_TRUE(batch.Contains(&item));
}

TEST(RefBatchHandle, MoveAssignment)
{
    RefBatch<TestItem> batch;
    TestItem a, b;

    RefBatchHandle<TestItem> handleA(&batch, &a);
    RefBatchHandle<TestItem> handleB(&batch, &b);

    handleA = std::move(handleB);

    EXPECT_FALSE(batch.Contains(&a));
    EXPECT_TRUE(batch.Contains(&b));
    EXPECT_TRUE(handleA.IsValid());
    EXPECT_FALSE(handleB.IsValid());
}

TEST(RefBatchHandle, DefaultConstructedIsInvalid)
{
    RefBatchHandle<TestItem> handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(RefBatchHandle, NullArrayIsNoOp)
{
    TestItem item;
    RefBatchHandle<TestItem> handle(nullptr, &item);
    EXPECT_FALSE(handle.IsValid());
}
