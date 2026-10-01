#include "authoring/AnimationExplorers.h"

#include <anim/Skeleton.h>

#include <gtest/gtest.h>

// The animation explorers' items: joints under their parents however the
// skeleton orders them, and keys that say what an item is.
TEST(AnimationExplorers, AJointSitsUnderItsParentEvenWhenSiblingsSubtreesInterleave)
{
    // hips(0) > spine(1), leg(2); spine > head(3); leg > foot(4): stored parents
    // first, but leg comes before spine's child.
    SkeletonData skeleton;
    skeleton.Joints = { { .Name = "hips" },
                        { .Name = "spine", .ParentIndex = 0 },
                        { .Name = "leg", .ParentIndex = 0 },
                        { .Name = "head", .ParentIndex = 1 },
                        { .Name = "foot", .ParentIndex = 2 } };
    const std::vector<OutlineNode> nodes = AnimationSkeletonNodes(skeleton, 3);
    std::vector<std::string> outline;
    for (const OutlineNode& node : nodes)
        outline.push_back(std::string(static_cast<std::size_t>(node.Depth), ' ') + node.Label);
    EXPECT_EQ(outline, (std::vector<std::string>{ "hips", " spine", "  head", " leg", "  foot" }));
    EXPECT_TRUE(nodes[2].Current) << "the picked joint";

    const std::optional<AnimationExplorerItem> head = ParseAnimationExplorerKey(nodes[2].Key);
    ASSERT_TRUE(head.has_value());
    EXPECT_EQ(head->What, AnimationExplorerItem::Kind::Joint);
    EXPECT_EQ(head->Joint, 3);
}

TEST(AnimationExplorers, AKeySaysWhatItNames)
{
    const std::optional<AnimationExplorerItem> clip = ParseAnimationExplorerKey("clip:asset://anim/walk.sanim");
    ASSERT_TRUE(clip.has_value());
    EXPECT_EQ(clip->What, AnimationExplorerItem::Kind::Clip);
    EXPECT_EQ(clip->Path, "asset://anim/walk.sanim");
    EXPECT_FALSE(ParseAnimationExplorerKey("joint:hips").has_value());
    EXPECT_FALSE(ParseAnimationExplorerKey("walk.sanim").has_value());
}
