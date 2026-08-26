// The two decisions inside RenderTargetStore that are about values rather than
// images: which slot a frame renders into, and whether a slot's images can
// still serve the descriptor asked for.
//
// Both have a bug history. Folding a frame onto slot 0 is what made the editor
// sample the target the next frame was rendering into, and a rebuild rule that
// only compares extent silently keeps images of the wrong format when a host
// renders into something other than what the target was first built for.

#include <gtest/gtest.h>

#include <graphics/vulkan/RenderTargetStore.h>

namespace
{

RenderTargetDesc Desc(std::uint32_t w, std::uint32_t h,
                      VkFormat color = VK_FORMAT_R16G16B16A16_SFLOAT,
                      VkFormat depth = VK_FORMAT_D32_SFLOAT)
{
    RenderTargetDesc desc{};
    desc.Extent = { w, h };
    desc.ColorFormat = color;
    desc.DepthFormat = depth;
    desc.Read = RenderTargetRead::Sampled;
    return desc;
}

} // namespace

TEST(ResolveTargetSlot, UsesTheFrameSlotWhenItIsInRange)
{
    EXPECT_EQ(ResolveTargetSlot(0, 3), 0u);
    EXPECT_EQ(ResolveTargetSlot(1, 3), 1u);
    EXPECT_EQ(ResolveTargetSlot(2, 3), 2u);
}

TEST(ResolveTargetSlot, NeverFallsBackToSlotZero)
{
    // The specific regression. An out-of-range index folding onto slot 0 puts
    // the render into the slot most likely to still be in flight, which
    // corrupts the displayed image instead of degrading it; the last slot is
    // the one furthest from the frame being presented.
    EXPECT_EQ(ResolveTargetSlot(9, 3), 2u);
    EXPECT_EQ(ResolveTargetSlot(3, 3), 2u);
    EXPECT_NE(ResolveTargetSlot(9, 3), 0u);
}

TEST(ResolveTargetSlot, ToleratesAStoreWithNoSlots)
{
    EXPECT_EQ(ResolveTargetSlot(0, 0), 0u);
    EXPECT_EQ(ResolveTargetSlot(7, 0), 0u);
}

TEST(TargetSlotNeedsRebuild, BuildsWhenThereAreNoImagesYet)
{
    EXPECT_TRUE(TargetSlotNeedsRebuild(RenderTargetDesc{}, Desc(800, 600), false));
}

TEST(TargetSlotNeedsRebuild, KeepsImagesThatAlreadyMatch)
{
    const RenderTargetDesc desc = Desc(800, 600);
    EXPECT_FALSE(TargetSlotNeedsRebuild(desc, desc, true));
}

TEST(TargetSlotNeedsRebuild, RebuildsOnAnyExtentChange)
{
    const RenderTargetDesc built = Desc(800, 600);
    EXPECT_TRUE(TargetSlotNeedsRebuild(built, Desc(801, 600), true));
    EXPECT_TRUE(TargetSlotNeedsRebuild(built, Desc(800, 601), true));
}

TEST(TargetSlotNeedsRebuild, RebuildsOnAFormatChange)
{
    // A host rendering into this target keys its pipelines on the attachment
    // formats. Serving images of the old format would mismatch the pipeline the
    // caller just built, which no extent comparison would catch.
    const RenderTargetDesc built = Desc(800, 600);
    EXPECT_TRUE(TargetSlotNeedsRebuild(
        built, Desc(800, 600, VK_FORMAT_B8G8R8A8_SRGB), true));
    EXPECT_TRUE(TargetSlotNeedsRebuild(
        built, Desc(800, 600, VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D16_UNORM), true));
}

TEST(TargetSlotNeedsRebuild, RebuildsWhenTheReadKindChanges)
{
    // Read kind decides image usage and whether a bindless index exists, both
    // baked in at creation.
    RenderTargetDesc built = Desc(800, 600);
    RenderTargetDesc wanted = built;
    wanted.Read = RenderTargetRead::Bindless;
    EXPECT_TRUE(TargetSlotNeedsRebuild(built, wanted, true));
}

TEST(TargetSlotNeedsRebuild, DoesNotBuildForAZeroExtent)
{
    // A hidden panel zeroes its rect. Building a zero-sized image is invalid
    // Vulkan, and Acquire reports the target as unavailable instead.
    EXPECT_FALSE(TargetSlotNeedsRebuild(RenderTargetDesc{}, Desc(0, 600), false));
    EXPECT_FALSE(TargetSlotNeedsRebuild(RenderTargetDesc{}, Desc(800, 0), false));
    EXPECT_FALSE(TargetSlotNeedsRebuild(RenderTargetDesc{}, Desc(0, 0), false));
}

TEST(TargetSlotNeedsRebuild, RebuildsAfterAFailedBuildLeftNoImages)
{
    // BuildSlot releases everything when the depth image fails, so the slot
    // reports no images while still holding the descriptor it tried. The next
    // acquire has to try again rather than read the stale descriptor as a
    // match.
    const RenderTargetDesc desc = Desc(800, 600);
    EXPECT_TRUE(TargetSlotNeedsRebuild(desc, desc, false));
}
