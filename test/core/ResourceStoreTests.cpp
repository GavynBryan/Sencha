#include <gtest/gtest.h>

#include <core/ResourceStore.h>

#include <type_traits>
#include <utility>
#include <vector>

namespace
{
    struct PlainResource
    {
        explicit PlainResource(int value)
            : Value(value)
        {
        }

        int Value = 0;
    };

    struct OtherResource
    {
        int Value = 7;
    };

    template <int Id>
    struct DestructionResource
    {
        explicit DestructionResource(std::vector<int>& order)
            : Order(&order)
        {
        }

        ~DestructionResource()
        {
            Order->push_back(Id);
        }

        std::vector<int>* Order = nullptr;
    };

    struct AnchorResource
    {
    };

    struct DependentResource
    {
        DependentResource(ResourceStore& store, bool& sawAnchor)
            : Store(&store)
            , SawAnchor(&sawAnchor)
        {
        }

        ~DependentResource()
        {
            *SawAnchor = Store->Has<AnchorResource>();
        }

        ResourceStore* Store = nullptr;
        bool* SawAnchor = nullptr;
    };
}

static_assert(!std::is_copy_constructible_v<ResourceStore>);
static_assert(!std::is_copy_assignable_v<ResourceStore>);
static_assert(std::is_nothrow_move_constructible_v<ResourceStore>);
static_assert(std::is_nothrow_move_assignable_v<ResourceStore>);

TEST(ResourceStore, RegisterAndGet)
{
    ResourceStore store;
    PlainResource& resource = store.Register<PlainResource>(42);

    EXPECT_EQ(&store.Get<PlainResource>(), &resource);
    EXPECT_EQ(store.Get<PlainResource>().Value, 42);
}

TEST(ResourceStore, ConstLookupPreservesConstness)
{
    ResourceStore store;
    store.Register<PlainResource>(42);
    const ResourceStore& constStore = store;

    static_assert(std::is_same_v<decltype(constStore.Get<PlainResource>()),
                                 const PlainResource&>);
    static_assert(std::is_same_v<decltype(constStore.TryGet<PlainResource>()),
                                 const PlainResource*>);
    EXPECT_EQ(constStore.Get<PlainResource>().Value, 42);
}

TEST(ResourceStore, TryGetReturnsNullWhenMissing)
{
    ResourceStore store;
    EXPECT_EQ(store.TryGet<PlainResource>(), nullptr);
}

TEST(ResourceStore, HasReportsRegisteredResource)
{
    ResourceStore store;
    store.Register<OtherResource>();

    EXPECT_TRUE(store.Has<OtherResource>());
    EXPECT_FALSE(store.Has<PlainResource>());
}

TEST(ResourceStore, EnsureReturnsExistingResource)
{
    ResourceStore store;
    PlainResource& first = store.Ensure<PlainResource>(3);
    PlainResource& second = store.Ensure<PlainResource>(9);

    EXPECT_EQ(&first, &second);
    EXPECT_EQ(second.Value, 3);
}

TEST(ResourceStore, RegisterDuplicateAsserts)
{
    ResourceStore store;
    store.Register<OtherResource>();

    EXPECT_DEATH(store.Register<OtherResource>(), "duplicate resource");
}

TEST(ResourceStore, MissingGetAsserts)
{
    ResourceStore store;
    EXPECT_DEATH(store.Get<OtherResource>(), "resource not registered");
}

TEST(ResourceStore, ClearDestroysResourcesInReverseRegistrationOrder)
{
    std::vector<int> order;
    ResourceStore store;
    store.Register<DestructionResource<1>>(order);
    store.Register<DestructionResource<2>>(order);
    store.Register<DestructionResource<3>>(order);

    store.Clear();

    ASSERT_EQ(order.size(), 3u);
    EXPECT_EQ(order[0], 3);
    EXPECT_EQ(order[1], 2);
    EXPECT_EQ(order[2], 1);

    store.Clear();
    EXPECT_EQ(order.size(), 3u);
}

TEST(ResourceStore, LaterResourceCanReachEarlierResourceDuringDestruction)
{
    bool sawAnchor = false;
    ResourceStore store;
    store.Register<AnchorResource>();
    store.Register<DependentResource>(store, sawAnchor);

    store.Clear();

    EXPECT_TRUE(sawAnchor);
}

TEST(ResourceStore, MoveConstructionTransfersOwnershipAndLookup)
{
    ResourceStore source;
    PlainResource& original = source.Register<PlainResource>(17);

    ResourceStore target(std::move(source));

    EXPECT_EQ(source.TryGet<PlainResource>(), nullptr);
    EXPECT_EQ(&target.Get<PlainResource>(), &original);
    EXPECT_EQ(target.Get<PlainResource>().Value, 17);
}

TEST(ResourceStore, MoveAssignmentClearsTargetBeforeTransfer)
{
    std::vector<int> order;
    ResourceStore source;
    source.Register<PlainResource>(23);

    ResourceStore target;
    target.Register<DestructionResource<1>>(order);
    target = std::move(source);

    ASSERT_EQ(order.size(), 1u);
    EXPECT_EQ(order[0], 1);
    EXPECT_EQ(source.TryGet<PlainResource>(), nullptr);
    EXPECT_EQ(target.Get<PlainResource>().Value, 23);
}
