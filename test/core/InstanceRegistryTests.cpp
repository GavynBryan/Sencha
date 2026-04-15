#include <gtest/gtest.h>
#include <core/batch/InstanceRegistry.h>
#include <core/handle/InstanceRegistryHandle.h>

// --- Test helpers ---

struct TestItem {
    int Value = 0;
};

// --- InstanceRegistry Tests ---

TEST(InstanceRegistry, AddAndContains)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    registry.Add(&item);
    EXPECT_TRUE(registry.Contains(&item));
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(InstanceRegistry, AddDuplicateIsIgnored)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    registry.Add(&item);
    registry.Add(&item);
    EXPECT_EQ(registry.Count(), 1u);
}

TEST(InstanceRegistry, RemoveItem)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    registry.Add(&item);
    registry.Remove(&item);

    EXPECT_FALSE(registry.Contains(&item));
    EXPECT_EQ(registry.Count(), 0u);
}

TEST(InstanceRegistry, RemoveNonExistentIsNoOp)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    registry.Remove(&item);
    EXPECT_EQ(registry.Count(), 0u);
}

TEST(InstanceRegistry, SwapAndPopPreservesOtherItems)
{
    InstanceRegistry<TestItem> registry;
    TestItem a{1}, b{2}, c{3};

    registry.Add(&a);
    registry.Add(&b);
    registry.Add(&c);

    registry.Remove(&a);

    EXPECT_EQ(registry.Count(), 2u);
    EXPECT_FALSE(registry.Contains(&a));
    EXPECT_TRUE(registry.Contains(&b));
    EXPECT_TRUE(registry.Contains(&c));
}

TEST(InstanceRegistry, GetItemsReturnsAllItems)
{
    InstanceRegistry<TestItem> registry;
    TestItem a{1}, b{2};

    registry.Add(&a);
    registry.Add(&b);

    auto items = registry.GetItems();
    EXPECT_EQ(items.size(), 2u);
}

TEST(InstanceRegistry, IsEmptyReportsCorrectly)
{
    InstanceRegistry<TestItem> registry;
    EXPECT_TRUE(registry.IsEmpty());

    TestItem item;
    registry.Add(&item);
    EXPECT_FALSE(registry.IsEmpty());
}

TEST(InstanceRegistry, ClearRemovesEverything)
{
    InstanceRegistry<TestItem> registry;
    TestItem a, b;

    registry.Add(&a);
    registry.Add(&b);
    registry.Clear();

    EXPECT_EQ(registry.Count(), 0u);
    EXPECT_TRUE(registry.IsEmpty());
    EXPECT_FALSE(registry.Contains(&a));
}

TEST(InstanceRegistry, DirtyFlag)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    EXPECT_FALSE(registry.CheckAndClearDirty());

    registry.Add(&item);
    EXPECT_TRUE(registry.CheckAndClearDirty());

    EXPECT_FALSE(registry.CheckAndClearDirty());

    registry.MarkDirty();
    EXPECT_TRUE(registry.CheckAndClearDirty());
}

TEST(InstanceRegistry, SortIfDirty)
{
    InstanceRegistry<TestItem> registry;
    TestItem a{3}, b{1}, c{2};

    registry.Add(&a);
    registry.Add(&b);
    registry.Add(&c);

    registry.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    auto items = registry.GetItems();
    ASSERT_EQ(items.size(), 3u);
    EXPECT_EQ(items[0]->Value, 1);
    EXPECT_EQ(items[1]->Value, 2);
    EXPECT_EQ(items[2]->Value, 3);
}

TEST(InstanceRegistry, SortIfDirtySkipsWhenClean)
{
    InstanceRegistry<TestItem> registry;
    TestItem a{2}, b{1};

    registry.Add(&a);
    registry.Add(&b);

    registry.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value < rhs->Value;
    });

    EXPECT_EQ(registry.GetItems()[0]->Value, 1);

    registry.SortIfDirty([](const TestItem* lhs, const TestItem* rhs) {
        return lhs->Value > rhs->Value;
    });

    EXPECT_EQ(registry.GetItems()[0]->Value, 1);
}

TEST(InstanceRegistry, RangeBasedForLoop)
{
    InstanceRegistry<TestItem> registry;
    TestItem a{10}, b{20};

    registry.Add(&a);
    registry.Add(&b);

    int sum = 0;
    for (const auto* item : registry) {
        sum += item->Value;
    }
    EXPECT_EQ(sum, 30);
}

// --- InstanceRegistryHandle Tests ---

TEST(InstanceRegistryHandle, AddsOnConstructionRemovesOnDestruction)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    {
        InstanceRegistryHandle<TestItem> handle(&registry, &item);
        EXPECT_TRUE(registry.Contains(&item));
        EXPECT_EQ(registry.Count(), 1u);
    }

    EXPECT_FALSE(registry.Contains(&item));
    EXPECT_EQ(registry.Count(), 0u);
}

TEST(InstanceRegistryHandle, ResetRemovesEarly)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    InstanceRegistryHandle<TestItem> handle(&registry, &item);
    EXPECT_TRUE(registry.Contains(&item));

    handle.Reset();
    EXPECT_FALSE(registry.Contains(&item));
    EXPECT_FALSE(handle.IsValid());
}

TEST(InstanceRegistryHandle, MoveConstructor)
{
    InstanceRegistry<TestItem> registry;
    TestItem item;

    InstanceRegistryHandle<TestItem> original(&registry, &item);
    InstanceRegistryHandle<TestItem> moved(std::move(original));

    EXPECT_FALSE(original.IsValid());
    EXPECT_TRUE(moved.IsValid());
    EXPECT_TRUE(registry.Contains(&item));
}

TEST(InstanceRegistryHandle, MoveAssignment)
{
    InstanceRegistry<TestItem> registry;
    TestItem a, b;

    InstanceRegistryHandle<TestItem> handleA(&registry, &a);
    InstanceRegistryHandle<TestItem> handleB(&registry, &b);

    handleA = std::move(handleB);

    EXPECT_FALSE(registry.Contains(&a));
    EXPECT_TRUE(registry.Contains(&b));
    EXPECT_TRUE(handleA.IsValid());
    EXPECT_FALSE(handleB.IsValid());
}

TEST(InstanceRegistryHandle, DefaultConstructedIsInvalid)
{
    InstanceRegistryHandle<TestItem> handle;
    EXPECT_FALSE(handle.IsValid());
    EXPECT_FALSE(static_cast<bool>(handle));
}

TEST(InstanceRegistryHandle, NullOwnerIsNoOp)
{
    TestItem item;
    InstanceRegistryHandle<TestItem> handle(nullptr, &item);
    EXPECT_FALSE(handle.IsValid());
}
