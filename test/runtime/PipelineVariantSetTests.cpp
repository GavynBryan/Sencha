// When a pipeline family may be reused, and what a half-failed build leaves
// behind.
//
// Four families in the renderer answered this independently and disagreed:
// some recorded the key before knowing every variant compiled, some left a
// partly-built family in place for a later completeness scan to catch. None of
// it was covered, because every one of those call sites needs a device to
// reach. The rule does not -- it is a key comparison and a loop -- so it is
// tested here against a build function that hands back fake handles.

#include <gtest/gtest.h>

#include <graphics/vulkan/PipelineVariantSet.h>
#include <render/ShadowDepthPass.h>

#include <vector>

namespace
{

// Distinct non-null handles standing in for compiled pipelines. The set only
// ever compares them against VK_NULL_HANDLE and hands them back.
VkPipeline FakePipeline(std::size_t index)
{
    return reinterpret_cast<VkPipeline>(0x1000 + index);
}

// Records which variants were asked for, so a test can tell "did not rebuild"
// apart from "rebuilt to the same values".
struct BuildRecorder
{
    std::vector<std::size_t> Requested;
    // Variant index that fails to compile, or none.
    std::optional<std::size_t> FailAt;

    VkPipeline operator()(std::size_t index)
    {
        Requested.push_back(index);
        if (FailAt.has_value() && index == *FailAt)
            return VK_NULL_HANDLE;
        return FakePipeline(index);
    }
};

constexpr AttachmentFormatKey kSwapchain{ VK_FORMAT_B8G8R8A8_SRGB, VK_FORMAT_D32_SFLOAT };
constexpr AttachmentFormatKey kOffscreen{ VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_D32_SFLOAT };

} // namespace

TEST(PipelineVariantSet, BuildsEveryVariantOnFirstUse)
{
    PipelineVariantSet<4, AttachmentFormatKey> set;
    BuildRecorder build;

    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));

    const std::vector<std::size_t> expected{ 0, 1, 2, 3 };
    EXPECT_EQ(build.Requested, expected);
    for (std::size_t index = 0; index < 4; ++index)
        EXPECT_EQ(set.Get(index), FakePipeline(index));
}

TEST(PipelineVariantSet, DoesNotRebuildForTheSameKey)
{
    PipelineVariantSet<4, AttachmentFormatKey> set;
    BuildRecorder build;

    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));
    build.Requested.clear();

    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));

    // The steady state. Callers assemble their pipeline description inside the
    // build function, so a rebuild here would allocate once a frame.
    EXPECT_TRUE(build.Requested.empty());
}

TEST(PipelineVariantSet, RebuildsTheWholeFamilyWhenTheKeyChanges)
{
    PipelineVariantSet<4, AttachmentFormatKey> set;
    BuildRecorder build;

    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));
    build.Requested.clear();

    EXPECT_TRUE(set.Ensure(kOffscreen, std::ref(build)));

    // Every variant, not only the ones whose description mentions the format:
    // the key describes the render target they were all compiled against.
    const std::vector<std::size_t> expected{ 0, 1, 2, 3 };
    EXPECT_EQ(build.Requested, expected);
}

TEST(PipelineVariantSet, TreatsADefaultConstructedKeyAsUnbuilt)
{
    // The trap this replaces: each family picked its own impossible value --
    // VK_FORMAT_UNDEFINED, a -1.0f bias -- to mean "nothing built yet". A set
    // that has built nothing must not match any key, including a default one.
    PipelineVariantSet<2, AttachmentFormatKey> set;
    BuildRecorder build;

    EXPECT_EQ(set.Get(0), VK_NULL_HANDLE);
    EXPECT_TRUE(set.Ensure(AttachmentFormatKey{}, std::ref(build)));

    const std::vector<std::size_t> expected{ 0, 1 };
    EXPECT_EQ(build.Requested, expected);
}

TEST(PipelineVariantSet, KeepsNothingWhenAVariantFailsToCompile)
{
    PipelineVariantSet<4, AttachmentFormatKey> set;
    BuildRecorder build;
    build.FailAt = 2;

    EXPECT_FALSE(set.Ensure(kSwapchain, std::ref(build)));

    // Not even the variants that did compile: a family is usable only whole,
    // and a caller that reads a surviving handle would bind a pipeline built
    // for a family the set has already reported as failed.
    for (std::size_t index = 0; index < 4; ++index)
        EXPECT_EQ(set.Get(index), VK_NULL_HANDLE);
    // Stops at the failure rather than compiling the rest for nothing.
    const std::vector<std::size_t> attempted{ 0, 1, 2 };
    EXPECT_EQ(build.Requested, attempted);
}

TEST(PipelineVariantSet, RetriesTheWholeFamilyAfterAFailedBuild)
{
    PipelineVariantSet<4, AttachmentFormatKey> set;
    BuildRecorder build;
    build.FailAt = 2;

    EXPECT_FALSE(set.Ensure(kSwapchain, std::ref(build)));
    build.Requested.clear();
    build.FailAt.reset();

    // The key was never recorded, so the same key is still a miss. A shader
    // reload or a device that recovers gets the family back.
    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));
    const std::vector<std::size_t> expected{ 0, 1, 2, 3 };
    EXPECT_EQ(build.Requested, expected);
}

TEST(PipelineVariantSet, ReportsNullPastTheEndOfTheFamily)
{
    // How the mesh pass indexes its two debug variants with a queue item's
    // pipeline id, which is numbered for the four-variant opaque family.
    PipelineVariantSet<2, AttachmentFormatKey> set;
    BuildRecorder build;
    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));

    EXPECT_EQ(set.Get(1), FakePipeline(1));
    EXPECT_EQ(set.Get(2), VK_NULL_HANDLE);
    EXPECT_EQ(set.Get(99), VK_NULL_HANDLE);
}

TEST(PipelineVariantSet, RebuildsAfterReset)
{
    PipelineVariantSet<2, AttachmentFormatKey> set;
    BuildRecorder build;
    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));

    set.Reset();
    EXPECT_EQ(set.Get(0), VK_NULL_HANDLE);

    build.Requested.clear();
    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));
    const std::vector<std::size_t> expected{ 0, 1 };
    EXPECT_EQ(build.Requested, expected);
}

TEST(PipelineVariantSet, DistinguishesFamiliesByEveryFieldOfTheKey)
{
    PipelineVariantSet<1, AttachmentFormatKey> set;
    BuildRecorder build;
    EXPECT_TRUE(set.Ensure(kSwapchain, std::ref(build)));
    build.Requested.clear();

    // Same colour target, different depth format: a real case, since editor
    // viewports and the swapchain do not share a depth format.
    const AttachmentFormatKey sameColorOtherDepth{ kSwapchain.Color, VK_FORMAT_D16_UNORM };
    EXPECT_TRUE(set.Ensure(sameColorOtherDepth, std::ref(build)));
    EXPECT_EQ(build.Requested.size(), 1u);
}

TEST(ShadowDepthBias, SeparatesFamiliesByExactBiasValue)
{
    // The shadow family keys on cvar-driven floats rather than formats, so
    // exact equality is the intent: a different bias is a different pipeline.
    PipelineVariantSet<kShadowPipelineCount, ShadowDepthBias> set;
    BuildRecorder build;

    EXPECT_TRUE(set.Ensure(ShadowDepthBias{ 1.25f, 2.5f }, std::ref(build)));
    build.Requested.clear();

    EXPECT_TRUE(set.Ensure(ShadowDepthBias{ 1.25f, 2.5f }, std::ref(build)));
    EXPECT_TRUE(build.Requested.empty());

    EXPECT_TRUE(set.Ensure(ShadowDepthBias{ 1.25f, 2.75f }, std::ref(build)));
    EXPECT_EQ(build.Requested.size(), kShadowPipelineCount);
}

TEST(SelectShadowPipeline, PrefersDoubleSidedOverTheFrontFaceFlip)
{
    // A cube face mirrors winding, so a single-sided caster in a point view
    // needs the flipped pipeline -- but a double-sided caster culls nothing,
    // which makes the flip irrelevant rather than a fourth variant.
    EXPECT_EQ(SelectShadowPipeline(false, false), ShadowPipelineId::Back);
    EXPECT_EQ(SelectShadowPipeline(false, true), ShadowPipelineId::FlippedBack);
    EXPECT_EQ(SelectShadowPipeline(true, false), ShadowPipelineId::DoubleSided);
    EXPECT_EQ(SelectShadowPipeline(true, true), ShadowPipelineId::DoubleSided);
}

TEST(SelectShadowPipeline, NumbersEveryVariantWithinTheFamily)
{
    for (const bool doubleSided : { false, true })
    {
        for (const bool flip : { false, true })
        {
            const auto id = static_cast<std::size_t>(SelectShadowPipeline(doubleSided, flip));
            EXPECT_LT(id, kShadowPipelineCount);
        }
    }
}
