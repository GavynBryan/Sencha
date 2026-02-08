#include <gtest/gtest.h>
#include <batch/BatchArray.h>
#include <batch/BatchArrayHandle.h>

// --- Test helpers ---

struct TestItem {
    int Value = 0;
};

// --- BatchArray Tests ---

TEST(BatchArray, AddAndContains)
{
    BatchArray<TestItem> array;
    TestItem item;

    array.Add(&item);
    EXPECT_TRUE(array.Contains(&item));
    EXPECT_EQ(array.Count(), 1u);
}

TEST(BatchArray, AddDuplicateIsIgnored)
{
    BatchArray<TestItem> array;
    TestItem item;

    array.Add(&item);
    array.Add(&item);
    EXPECT_EQ(array.Count(), 1u);
}

TEST(BatchArray, RemoveItem)
{
    BatchArray<TestItem> array;
    TestItem item;

    array.Add(&item);
    array.Remove(&item);

    EXPECT_FALSE(array.Contains(&item));
    EXPECT_EQ(array.Count(), 0u);
}

TEST(BatchArray, RemoveNonExistentIsNoOp)
{
    BatchArray<TestItem> array;
    TestItem item;

    // Should not crash
    array.Remove(&item);
    EXPECT_EQ(array.Count(), 0u);
}

TEST(BatchArray, SwapAndPopPreservesOtherItems)
{
    BatchArray<TestItem> array;
    TestItem a{1}, b{2}, c{3};

    array.Add(&a);
    array.Add(&b);
    array.Add(&c);

    // Remove the first item — should swap with last
    array.Remove(&a);

    EXPECT_EQ(array.Count(), 2u);
    EXPECT_FALSE(array.Contains(&a));
    EXPECT_TRUE(array.Contains(&b));
    EXPECT_TRUE(array.Contains(&c));
}

TEST(BatchArray, GetItemsReturnsAllItems)
{
    BatchArray<TestItem> array;
    TestItem a{1}, b{2};

    array.Add(&a);
    array.Add(&b);

    auto items = array.GetItems();
    EXPECT_EQ(items.size(), 2u);
}

TEST(BatchArray, IsEmptyReportsCorrectly)
{
    BatchArray<TestItem> array;
    EXPECT_TRUE(array.IsEmpty());

    TestItem item;
    array.Add(&item);
    EXPECT_FALSE(array.IsEmpty());
}

TEST(BatchArray, ClearRemovesEverything)
{
    BatchArray<TestItem> array;
    TestItem a, b;

    array.Add(&a);
    array.Add(&b);
    array.Clear();

    EXPECT_EQ(array.Count(), 0u);
    EXPECT_TRUE(array.IsEmpty());
    EXPECT_FALSE(array.Contains(&a));
}

TEST(BatchArray, DirtyFlag)
{
    BatchArray<TestItem> array;
    TestItem item;

    // Fresh array is not dirty
    EXPECT_FALSE(array.CheckAndClearDirty());

    // Add sets dirty
    array.Add(&item);
    EXPECT_TRUE(array.CheckAndClearDirty());

    // CheckAndClear clears it
    EXPECT_FALSE(array.CheckAndClearDirty());

    // MarkDirty sets it again
    array.MarkDirty();
    EXPECT_TRUE(array.CheckAndClearDirty());
}

TEST(BatchArray, SortIfDirty)
{
    BatchArray<TestItem> array;
    TestItem a{3}, b{1}, c{2};

    array.Add(&a);
    array.Add(&b);
    array.Add(&c);

    array.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    auto items = array.GetItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0]->Value, 1);
    EXPECT_EQ(items[1]->Value, 2);
    EXPECT_EQ(items[2]->Value, 3);
}

TEST(BatchArray, SortIfDirtySkipsWhenClean)
{
    BatchArray<TestItem> array;
    TestItem a{2}, b{1};

    array.Add(&a);
    array.Add(&b);

    // Sort once (clears dirty)
    array.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    // Items are now sorted [1, 2]
    EXPECT_EQ(array.GetItems()[0]->Value, 1);

    // Sort again with reverse comparator — should NOT re-sort since not dirty
    array.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value > rhs->Value;
    });

    // Still [1, 2] because it wasn't dirty
    EXPECT_EQ(array.GetItems()[0]->Value, 1);
}

TEST(BatchArray, RangeBasedForLoop)
{
    BatchArray<TestItem> array;
    TestItem a{10}, b{20};

    array.Add(&a);
    array.Add(&b);

    int sum = 0;
    for (const auto* item : array) {
        sum += item->Value;
    }
    EXPECT_EQ(sum, 30);
}

// --- BatchArrayHandle Tests ---

TEST(BatchArrayHandle, AddsOnConstructionRemovesOnDestruction)
{
    BatchArray<TestItem> array;
    TestItem item;

    {
        BatchArrayHandle handle(&array, &item);
        EXPECT_TRUE(array.Contains(&item));
        EXPECT_EQ(array.Count(), 1u);
    }

    // After handle goes out of scope
    EXPECT_FALSE(array.Contains(&item));
    EXPECT_EQ(array.Count(), 0u);
}

TEST(BatchArrayHandle, ResetRemovesEarly)
{
    BatchArray<TestItem> array;
    TestItem item;

    BatchArrayHandle handle(&array, &item);
    EXPECT_TRUE(array.Contains(&item));

    handle.Reset();
    EXPECT_FALSE(array.Contains(&item));
    EXPECT_FALSE(handle.IsValid());
}

TEST(BatchArrayHandle, MoveConstructor)
{
    BatchArray<TestItem> array;
    TestItem item;

    BatchArrayHandle original(&array, &item);
    BatchArrayHandle moved(std::move(original));

    EXPECT_FALSE(original.IsValid());
    EXPECT_TRUE(moved.IsValid());
    EXPECT_TRUE(array.Contains(&item));
}

TEST(BatchArrayHandle, MoveAssignment)
{
    BatchArray<TestItem> array;
    TestItem a, b;

    BatchArrayHandle handleA(&array, &a);
    BatchArrayHandle handleB(&array, &b);

    // Move-assign B into A — should remove 'a', keep 'b'
    handleA = std::move(handleB);

    EXPECT_FALSE(array.Contains(&a));
    EXPECT_TRUE(array.Contains(&b));
    EXPECT_TRUE(handleA.IsValid());
    EXPECT_FALSE(handleB.IsValid());
}

TEST(BatchArrayHandle, DefaultConstructedIsInvalid)
{
    BatchArrayHandle handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(BatchArrayHandle, NullArrayIsNoOp)
{
    TestItem item;
    BatchArrayHandle handle(nullptr, &item);
    EXPECT_FALSE(handle.IsValid());
    // Destruction should not crash
}
