// The frame context a rendering scope publishes to whatever draws inside it.
//
// Three sites hand-built this before RenderScope existed, and a hand-built
// context silently zeroes every field it does not mention. That has already
// bitten: when the fence-anchored retirement clock was added to FrameContext,
// two of those three would have handed their passes a clock reading zero --
// "nothing has ever retired" -- which frees a resource the GPU is still using.
//
// So the assertion that matters is not any single field. It is that the inner
// context is *derived from* the outer one, and that a field nobody here knows
// about survives the trip.

#include <gtest/gtest.h>

#include <graphics/vulkan/RenderScope.h>

namespace
{

FrameContext MakeOuterFrame()
{
    FrameContext outer{};
    outer.Cmd = reinterpret_cast<VkCommandBuffer>(0x1234);
    outer.FrameInFlightIndex = 2;
    outer.TargetExtent = { 1920, 1080 };
    outer.TargetFormat = VK_FORMAT_B8G8R8A8_SRGB;
    outer.DepthView = reinterpret_cast<VkImageView>(0x5678);
    outer.DepthFormat = VK_FORMAT_D32_SFLOAT;
    outer.Phase = RenderPhase::MainColor;
    outer.Retirement = GpuFrameRetirement{ .Current = 9, .RetiredThrough = 7 };
    return outer;
}

RenderScopeDesc MakeOffscreenDesc()
{
    RenderScopeDesc desc{};
    desc.Area.offset = { 0, 0 };
    desc.Area.extent = { 512, 256 };
    desc.Color.View = reinterpret_cast<VkImageView>(0xAAAA);
    desc.ColorFormat = VK_FORMAT_R16G16B16A16_SFLOAT;
    desc.Depth.View = reinterpret_cast<VkImageView>(0xBBBB);
    desc.DepthFormat = VK_FORMAT_D16_UNORM;
    desc.Phase = RenderPhase::Offscreen;
    return desc;
}

} // namespace

TEST(MakeScopeContext, CarriesTheCommandBufferAndSlotFromTheOuterFrame)
{
    const FrameContext outer = MakeOuterFrame();
    const FrameContext inner = MakeScopeContext(outer, MakeOffscreenDesc());

    EXPECT_EQ(inner.Cmd, outer.Cmd);
    EXPECT_EQ(inner.FrameInFlightIndex, outer.FrameInFlightIndex);
}

TEST(MakeScopeContext, CarriesTheRetirementClockThrough)
{
    const FrameContext outer = MakeOuterFrame();
    const FrameContext inner = MakeScopeContext(outer, MakeOffscreenDesc());

    // The specific field that a hand-built context dropped. A zeroed clock
    // reads as "nothing has ever retired", which frees resources still in use.
    EXPECT_EQ(inner.Retirement, outer.Retirement);
    EXPECT_TRUE(inner.Retirement.IsRetired(6));
}

TEST(MakeScopeContext, TakesTheTargetDescriptionFromTheScope)
{
    const RenderScopeDesc desc = MakeOffscreenDesc();
    const FrameContext inner = MakeScopeContext(MakeOuterFrame(), desc);

    // The whole point of a nested scope: the inner target is not the outer one,
    // and pipelines keyed on attachment format must see the inner formats.
    EXPECT_EQ(inner.TargetExtent.width, 512u);
    EXPECT_EQ(inner.TargetExtent.height, 256u);
    EXPECT_EQ(inner.TargetFormat, VK_FORMAT_R16G16B16A16_SFLOAT);
    EXPECT_EQ(inner.DepthView, desc.Depth.View);
    EXPECT_EQ(inner.DepthFormat, VK_FORMAT_D16_UNORM);
    EXPECT_EQ(inner.Phase, RenderPhase::Offscreen);
}

TEST(MakeScopeContext, ReportsNoDepthWhenTheScopeAttachesNone)
{
    RenderScopeDesc desc = MakeOffscreenDesc();
    desc.Depth = RenderScopeAttachment{};
    desc.DepthFormat = VK_FORMAT_UNDEFINED;

    const FrameContext inner = MakeScopeContext(MakeOuterFrame(), desc);

    // The outer frame had a depth target; a depth-less scope must not inherit
    // it, or a pass inside would bind an attachment the scope never opened.
    EXPECT_EQ(inner.DepthView, VK_NULL_HANDLE);
    EXPECT_EQ(inner.DepthFormat, VK_FORMAT_UNDEFINED);
}

TEST(MakeScopeContext, ReportsTheAreaExtentEvenWithAnOffsetOrigin)
{
    // A shadow view renders into a tile inside its atlas. The extent is the
    // tile's, not the atlas's, and the offset does not enter the context.
    RenderScopeDesc desc{};
    desc.Area.offset = { 1024, 512 };
    desc.Area.extent = { 512, 512 };
    desc.Depth.View = reinterpret_cast<VkImageView>(0xCCCC);
    desc.DepthFormat = VK_FORMAT_D16_UNORM;

    const FrameContext inner = MakeScopeContext(MakeOuterFrame(), desc);
    EXPECT_EQ(inner.TargetExtent.width, 512u);
    EXPECT_EQ(inner.TargetExtent.height, 512u);
    EXPECT_EQ(inner.TargetFormat, VK_FORMAT_UNDEFINED) << "depth-only scope";
}

TEST(MakeScopeContext, PreservesOuterFieldsItDoesNotOwn)
{
    // Stand-in for the next field added to FrameContext: anything the scope
    // does not explicitly set has to survive, because the alternative is
    // rediscovering the retirement-clock bug with a different field.
    FrameContext outer = MakeOuterFrame();
    outer.FrameInFlightIndex = 3;
    outer.Retirement.Current = 41;

    const FrameContext inner = MakeScopeContext(outer, MakeOffscreenDesc());
    EXPECT_EQ(inner.FrameInFlightIndex, 3u);
    EXPECT_EQ(inner.Retirement.Current, 41u);
}
