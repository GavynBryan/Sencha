// Finding an asset or a gameplay tag by name reads the name where it lies; a
// lookup that copied it into a key would allocate once the name outgrew the
// small-string buffer.

#include "AllocationCounter.h"

#include <anim/SkeletonCache.h>
#include <gameplay_tags/GameplayTagRegistry.h>

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

namespace
{
    constexpr std::string_view kLongPath = "asset://characters/hero/hero_skeleton.sskel";
    constexpr std::string_view kLongTag = "Anim.Locomotion.Traversal.Mantle.High";
}

TEST(NameLookupAllocation, FindingAnAssetByPathAllocatesNothing)
{
    SkeletonCache skeletons;
    (void)skeletons.Register(kLongPath, SkeletonData{});
    const std::size_t before = AllocationCount();
    const SkeletonHandle found = skeletons.Find(kLongPath);
    const bool missing = skeletons.Find("asset://characters/hero/nobody_skeleton.sskel").IsValid();
    EXPECT_EQ(AllocationCount() - before, 0u);
    EXPECT_TRUE(found.IsValid());
    EXPECT_FALSE(missing);
}

TEST(NameLookupAllocation, FindingATagByNameAllocatesNothing)
{
    GameplayTagRegistry tags;
    const std::optional<GameplayTagId> registered = tags.RegisterTag(kLongTag);
    ASSERT_TRUE(registered.has_value());
    const std::size_t before = AllocationCount();
    const GameplayTagId found = tags.FindTag(kLongTag);
    EXPECT_EQ(AllocationCount() - before, 0u);
    EXPECT_EQ(found, *registered);
}
