#include <core/handle/Handle.h>
#include <core/identity/StrongId.h>

#include <gtest/gtest.h>

#include <cstdint>
#include <type_traits>
#include <unordered_set>

namespace
{
    using FooHandle = Handle<struct FooTag>;
    using BarHandle = Handle<struct BarTag>;

    using FooId = StrongId<struct FooIdTag, uint64_t>;
    using BarId = StrongId<struct BarIdTag, uint32_t>;
}

// -- Handle -------------------------------------------------------------------

TEST(Handle, ValidityAndNull)
{
    FooHandle null;
    EXPECT_FALSE(null.IsValid());
    EXPECT_TRUE(null.IsNull());

    FooHandle live{ 3, 1 };
    EXPECT_TRUE(live.IsValid());
    EXPECT_FALSE(live.IsNull());
    EXPECT_EQ(SlotIndex(live), 3u);

    // Either field zero is invalid (matches the reserved-null-slot convention).
    EXPECT_FALSE((FooHandle{ 0, 1 }.IsValid()));
    EXPECT_FALSE((FooHandle{ 3, 0 }.IsValid()));
}

TEST(Handle, EqualityAndDistinctTypes)
{
    EXPECT_EQ((FooHandle{ 2, 5 }), (FooHandle{ 2, 5 }));
    EXPECT_NE((FooHandle{ 2, 5 }), (FooHandle{ 2, 6 }));

    // Different tags produce different, non-interchangeable types.
    static_assert(!std::is_same_v<FooHandle, BarHandle>);
    static_assert(!std::is_convertible_v<FooHandle, BarHandle>);
    static_assert(sizeof(FooHandle) == 8); // split 32/32, the unified layout
}

// -- StrongId -----------------------------------------------------------------

TEST(StrongId, ValidityEqualityHash)
{
    FooId zero;
    EXPECT_FALSE(zero.IsValid());
    EXPECT_FALSE(static_cast<bool>(zero));

    FooId a{ 42 };
    FooId b{ 42 };
    FooId c{ 7 };
    EXPECT_TRUE(a.IsValid());
    EXPECT_EQ(a, b);
    EXPECT_NE(a, c);

    std::unordered_set<FooId> set{ a, c };
    EXPECT_TRUE(set.contains(b)); // a == b hashes equal
    EXPECT_EQ(set.size(), 2u);
}

TEST(StrongId, DistinctTagsAndWidths)
{
    static_assert(!std::is_same_v<FooId, BarId>);
    static_assert(sizeof(FooId::Value) == sizeof(uint64_t));
    static_assert(sizeof(BarId::Value) == sizeof(uint32_t));
}
