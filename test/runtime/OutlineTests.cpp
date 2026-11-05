#include <gtest/gtest.h>

#include <ecs/World.h>
#include <render/RenderQueue.h>
#include <render/outline/OutlineContributions.h>

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
    ClearOutline(f.Entities, f.Target, Writer(2));
    EXPECT_EQ(f.Resolved(), f.Low);
    ClearOutlinesOf(f.Entities, PresentationWriterId{ 1 });
    EXPECT_FALSE(f.Resolved().IsValid());
    EXPECT_TRUE(f.Frame.IsEmpty());
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
    frame.Finish();
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
    f.Frame.Finish();
    ASSERT_EQ(f.Frame.DrawOrder.size(), 2u);
    EXPECT_EQ(f.Frame.DrawOrder[0], f.Low);
    EXPECT_EQ(f.Frame.DrawOrder[1], f.High);
}
