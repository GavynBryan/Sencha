#include <gtest/gtest.h>
#include <world/ResourceRegistry.h>

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
}

TEST(ResourceRegistry, RegisterAndGet)
{
    ResourceRegistry registry;
    PlainResource& resource = registry.Register<PlainResource>(42);

    EXPECT_EQ(&registry.Get<PlainResource>(), &resource);
    EXPECT_EQ(registry.Get<PlainResource>().Value, 42);
}

TEST(ResourceRegistry, TryGetReturnsNullWhenMissing)
{
    ResourceRegistry registry;
    EXPECT_EQ(registry.TryGet<PlainResource>(), nullptr);
}

TEST(ResourceRegistry, HasReportsRegisteredResource)
{
    ResourceRegistry registry;
    registry.Register<OtherResource>();

    EXPECT_TRUE(registry.Has<OtherResource>());
    EXPECT_FALSE(registry.Has<PlainResource>());
}

TEST(ResourceRegistry, EnsureReturnsExistingResource)
{
    ResourceRegistry registry;
    PlainResource& first = registry.Ensure<PlainResource>(3);
    PlainResource& second = registry.Ensure<PlainResource>(9);

    EXPECT_EQ(&first, &second);
    EXPECT_EQ(second.Value, 3);
}

TEST(ResourceRegistry, RegisterDuplicateAsserts)
{
    ResourceRegistry registry;
    registry.Register<OtherResource>();

    EXPECT_DEATH(registry.Register<OtherResource>(), "duplicate resource");
}
