#include <gtest/gtest.h>

#include <ecs/World.h>
#include <render/RenderQueue.h>
#include <render/SkinnedPoseFrameData.h>
#include <render/outline/OutlineContributions.h>
#include <render/outline/OutlineFrame.h>
#include <graphics/vulkan/OutlineMaskTargets.h>

#include <array>
#include <cstddef>

namespace
{
    OutlineStyleDesc Style(std::int16_t priority)
    {
        return { .Color = Vec<3>(1.0f, 0.5f, 0.0f), .WidthPixels = 2.0f, .Priority = priority };
    }

    PresentationOwner Writer(std::uint32_t writer, EntityId holder = {})
    {
        return { .Writer = PresentationWriterId{ writer }, .Holder = LeaseOwner{ .Entity = holder, .Key = 0 } };
    }

    OutlineContribution Outline(PresentationOwner owner, OutlineStyleId style, std::int16_t order = 0)
    {
        return { .Owner = owner, .Style = style, .Order = order };
    }

    struct Fixture
    {
        World Entities;
        OutlineStyleId Low = RegisterOutlineStyle(Entities, "low", Style(1));
        OutlineStyleId High = RegisterOutlineStyle(Entities, "high", Style(2));
        EntityId Target = Entities.CreateEntity();
        OutlineFrame Frame;

        [[nodiscard]] OutlineStyleId Resolved()
        {
            ExtractOutlines(Entities, Frame);
            const auto found = Frame.StyleOf.find(Target);
            return found != Frame.StyleOf.end() ? found->second : OutlineStyleId{};
        }
    };
}

TEST(OutlineStyles, RefusesADuplicatePriorityAndASixteenthStyle)
{
    World world;
    ASSERT_TRUE(RegisterOutlineStyle(world, "first", Style(0)).IsValid());
    EXPECT_FALSE(RegisterOutlineStyle(world, "again", Style(0)).IsValid());
    for (std::int16_t priority = 1; priority < static_cast<std::int16_t>(kMaxOutlineStyles); ++priority)
        EXPECT_EQ(RegisterOutlineStyle(world, "more", Style(priority)).Slot, priority + 1);
    EXPECT_FALSE(RegisterOutlineStyle(world, "sixteenth", Style(99)).IsValid());
}

TEST(OutlineStyles, AColourIsAuthoredInSrgbAndHeldInLinear)
{
    Fixture f;
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.Low));
    ExtractOutlines(f.Entities, f.Frame);
    const OutlineStyleEntry& style = f.Frame.Styles[f.Low.Slot];
    EXPECT_FLOAT_EQ(style.LinearColor.X, 1.0f);
    EXPECT_NEAR(style.LinearColor.Y, 0.2140f, 0.001f) << "sRGB 0.5";
    EXPECT_FLOAT_EQ(f.Frame.MaxWidthPixels, 2.0f);
}

TEST(Outlines, AnEntityHasOneStyleTheHighestOrderThenTheOwnerThatPrecedes)
{
    Fixture f;
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.Low, 0));
    SetOutline(f.Entities, f.Target, Outline(Writer(2), f.High, 5));
    EXPECT_EQ(f.Resolved(), f.High);

    for (const bool lowFirst : { true, false })
    {
        Fixture tied;
        const OutlineContribution a = Outline(Writer(1), tied.Low);
        const OutlineContribution b = Outline(Writer(2), tied.High);
        SetOutline(tied.Entities, tied.Target, lowFirst ? a : b);
        SetOutline(tied.Entities, tied.Target, lowFirst ? b : a);
        EXPECT_EQ(tied.Resolved(), tied.Low) << "whatever arrived first";
    }
}

TEST(Outlines, ClearingOneOwnerRevealsTheNextAndClearingAWriterTakesAllOfIts)
{
    Fixture f;
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.Low, 0));
    SetOutline(f.Entities, f.Target, Outline(Writer(2), f.High, 5));
    EXPECT_TRUE(IsOutlined(f.Entities, f.Target));
    ClearOutline(f.Entities, f.Target, Writer(2));
    EXPECT_EQ(f.Resolved(), f.Low);
    ClearOutlinesOf(f.Entities, PresentationWriterId{ 1 });
    EXPECT_FALSE(f.Resolved().IsValid());
    EXPECT_TRUE(f.Frame.IsEmpty());
    EXPECT_FALSE(IsOutlined(f.Entities, f.Target));
}

TEST(Outlines, ADeadTargetAndADeadHolderAreSweptBeforeResolving)
{
    Fixture f;
    const EntityId holder = f.Entities.CreateEntity();
    const EntityId other = f.Entities.CreateEntity();
    SetOutline(f.Entities, f.Target, Outline(Writer(1, holder), f.Low));
    SetOutline(f.Entities, other, Outline(Writer(1), f.Low));
    ExtractOutlines(f.Entities, f.Frame);
    EXPECT_EQ(f.Frame.StyleOf.size(), 2u);

    f.Entities.DestroyEntity(holder);
    EXPECT_FALSE(f.Resolved().IsValid()) << "its holder went";

    f.Entities.DestroyEntity(other);
    ExtractOutlines(f.Entities, f.Frame);
    EXPECT_TRUE(f.Frame.IsEmpty()) << "its target went";
}

TEST(Outlines, NothingHighlightedLeavesNoDrawsAndAWorldWithNoStoreIsFine)
{
    World world;
    OutlineFrame frame;
    ExtractOutlines(world, frame);
    frame.Finish(Mat4::Identity());
    EXPECT_TRUE(frame.IsEmpty());
    EXPECT_TRUE(frame.DrawOrder.empty());
    EXPECT_EQ(frame.QueueFor(world.CreateEntity()), nullptr);
}

TEST(Outlines, DrawOrderIsLowestPriorityFirstSoTheHighestWinsADepthTie)
{
    Fixture f;
    const EntityId other = f.Entities.CreateEntity();
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.High));
    SetOutline(f.Entities, other, Outline(Writer(1), f.Low));
    ExtractOutlines(f.Entities, f.Frame);
    ASSERT_NE(f.Frame.QueueFor(f.Target), nullptr);
    RenderQueueItem item;
    f.Frame.QueueFor(f.Target)->AddOpaque(item);
    f.Frame.QueueFor(other)->AddOpaque(item);
    f.Frame.Finish(Mat4::Identity());
    ASSERT_EQ(f.Frame.DrawOrder.size(), 2u);
    EXPECT_EQ(f.Frame.DrawOrder[0], f.Low);
    EXPECT_EQ(f.Frame.DrawOrder[1], f.High);
}

TEST(Outlines, TheScreenBoundsCoverTheQueuedItemsAndAnItemAcrossTheEyePlaneTakesTheView)
{
    Fixture f;
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.Low));
    ExtractOutlines(f.Entities, f.Frame);
    RenderQueueItem item;
    item.WorldBounds = Aabb3d(Vec3d(-0.25f, -0.5f, 0.0f), Vec3d(0.5f, 0.25f, 0.5f));
    f.Frame.QueueFor(f.Target)->AddOpaque(item);
    f.Frame.Finish(Mat4::Identity());
    EXPECT_FLOAT_EQ(f.Frame.ScreenMin[0], -0.25f);
    EXPECT_FLOAT_EQ(f.Frame.ScreenMin[1], -0.5f);
    EXPECT_FLOAT_EQ(f.Frame.ScreenMax[0], 0.5f);
    EXPECT_FLOAT_EQ(f.Frame.ScreenMax[1], 0.25f);

    // w = z - 0.25: the item's near face is behind the eye and its far face in front.
    Mat4 across = Mat4::Identity();
    across[3][2] = 1.0f;
    across[3][3] = -0.25f;
    f.Frame.Finish(across);
    EXPECT_FLOAT_EQ(f.Frame.ScreenMin[0], -1.0f);
    EXPECT_FLOAT_EQ(f.Frame.ScreenMax[1], 1.0f);

    // Wholly behind the eye, it covers nothing, so the composite has nowhere to draw.
    Mat4 behind = Mat4::Identity();
    behind[3][3] = -1.0f;
    f.Frame.Finish(behind);
    EXPECT_GT(f.Frame.ScreenMin[0], f.Frame.ScreenMax[0]);
}

TEST(Outlines, NothingOnScreenGivesAnEmptyScissor)
{
    Fixture f;
    SetOutline(f.Entities, f.Target, Outline(Writer(1), f.Low));
    ExtractOutlines(f.Entities, f.Frame);
    RenderQueueItem item;
    item.WorldBounds = Aabb3d(Vec3d(-0.25f, -0.5f, 0.0f), Vec3d(0.5f, 0.25f, 0.5f));
    f.Frame.QueueFor(f.Target)->AddOpaque(item);
    Mat4 behind = Mat4::Identity();
    behind[3][3] = -1.0f;
    f.Frame.Finish(behind);
    const VkRect2D scissor = OutlineScissorFor(f.Frame.CompositeInputs(), VkExtent2D{ 1920, 1080 });
    EXPECT_EQ(scissor.extent.width, 0u);
    EXPECT_EQ(scissor.extent.height, 0u);
}

TEST(PosedWorldBounds, HoldsEveryVertexBlendedBetweenItsJoints)
{
    const Aabb3d rest(Vec3d(-1.0f, -1.0f, -1.0f), Vec3d(1.0f, 1.0f, 1.0f));
    Mat4 raised = Mat4::Identity();
    raised[1][3] = 5.0f;
    const std::array<Mat4, 2> palette{ Mat4::Identity(), raised };
    Mat4 world = Mat4::Identity();
    world[0][3] = 10.0f;

    const Aabb3d bounds = PosedWorldBounds(rest, world, palette);
    EXPECT_FLOAT_EQ(bounds.Min.X, 9.0f);
    EXPECT_FLOAT_EQ(bounds.Max.X, 11.0f);
    EXPECT_FLOAT_EQ(bounds.Min.Y, -1.0f);
    EXPECT_FLOAT_EQ(bounds.Max.Y, 6.0f) << "the raised joint's reach, which the rest bounds miss";

    // A corner weighted half to each joint lands between them.
    const Vec4 corner(1.0f, 1.0f, 1.0f, 1.0f);
    const Vec4 first = world * palette[0] * corner;
    const Vec4 second = world * palette[1] * corner;
    const float blendedY = (first.Y + second.Y) * 0.5f;
    EXPECT_GE(blendedY, bounds.Min.Y);
    EXPECT_LE(blendedY, bounds.Max.Y);
}
