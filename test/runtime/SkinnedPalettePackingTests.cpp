#include <gtest/gtest.h>

#include <graphics/FrameScratchRing.h>
#include <render/SkinnedPoseFrameData.h>

#include <cstdint>
#include <vector>

// The pose dispatch hands each palette's byte offset to a storage-buffer
// descriptor, so every offset has to be a legal one on the strictest
// conformant device. These pin the packing that guarantees it without the
// extraction path querying a device it is not allowed to name.

namespace
{
constexpr std::uint64_t kMatrixBytes = sizeof(Mat4);

SkinnedPoseInstance AppendJoints(SkinnedPoseFrameData& data, std::uint32_t joints)
{
    const std::uint32_t slot =
        data.AppendInstance(SkinnedMeshHandle{}, RenderEntityKey{}, joints);
    return data.Instances[slot];
}
} // namespace

TEST(SkinnedPalettePacking, EveryPaletteStartsOnADescriptorLegalBoundary)
{
    // Joint counts chosen so a tightly packed layout would land three of the
    // four palettes on illegal offsets.
    SkinnedPoseFrameData data;
    const std::vector<std::uint32_t> jointCounts{ 17, 5, 64, 1 };
    for (const std::uint32_t joints : jointCounts)
    {
        const SkinnedPoseInstance instance = AppendJoints(data, joints);
        const std::uint64_t byteOffset =
            static_cast<std::uint64_t>(instance.PaletteOffset) * kMatrixBytes;
        EXPECT_EQ(byteOffset % kMaxDescriptorOffsetAlignment, 0u)
            << "palette at element " << instance.PaletteOffset
            << " is not a legal storage-buffer offset";
    }
}

TEST(SkinnedPalettePacking, PaletteOffsetsAdvanceByAlignedJointCounts)
{
    SkinnedPoseFrameData data;
    EXPECT_EQ(AppendJoints(data, 17).PaletteOffset, 0u);   // 17 joints end at 17
    EXPECT_EQ(AppendJoints(data, 5).PaletteOffset, 20u);   // 17 rounds up to 20; ends at 25
    EXPECT_EQ(AppendJoints(data, 64).PaletteOffset, 28u);  // 25 rounds up to 28; ends at 92
    EXPECT_EQ(AppendJoints(data, 1).PaletteOffset, 92u);   // 92 is already aligned
}

TEST(SkinnedPalettePacking, AlignmentGapsAreIdentityNotUninitialized)
{
    // A gap the dispatch never reads is still scratch memory the next frame
    // reuses, so it must carry the bind pose rather than whatever was there.
    SkinnedPoseFrameData data;
    for (Mat4& matrix : data.Palettes)
        matrix = Mat4{};

    AppendJoints(data, 3);
    AppendJoints(data, 2);

    ASSERT_EQ(data.Palettes.size(), 6u);
    EXPECT_EQ(data.Palettes[3], Mat4::Identity());
    EXPECT_EQ(data.Instances[1].PaletteOffset, 4u);
}

TEST(SkinnedPalettePacking, PoseSlotsIndexInstancesInAppendOrder)
{
    SkinnedPoseFrameData data;
    EXPECT_EQ(data.AppendInstance(SkinnedMeshHandle{}, RenderEntityKey{}, 4), 0u);
    EXPECT_EQ(data.AppendInstance(SkinnedMeshHandle{}, RenderEntityKey{}, 4), 1u);
    EXPECT_EQ(data.Instances.size(), 2u);
}

TEST(SkinnedPalettePacking, ResetClearsPackingState)
{
    SkinnedPoseFrameData data;
    AppendJoints(data, 7);
    data.Reset();
    EXPECT_TRUE(data.Instances.empty());
    EXPECT_TRUE(data.Palettes.empty());
    EXPECT_EQ(AppendJoints(data, 7).PaletteOffset, 0u);
}

// The ring aligns cursors relative to a slice, so a slice size that is not a
// multiple of the binding alignment makes every slice after the first serve
// absolutely misaligned offsets.
TEST(ScratchSliceBytes, SliceBoundariesLandOnDescriptorAlignment)
{
    EXPECT_EQ(ResolveScratchSliceBytes(1024 * 1024, 64), 1024u * 1024u);
    EXPECT_EQ(ResolveScratchSliceBytes(1024 * 1024 + 100, 64), 1024u * 1024u + 256u);
    EXPECT_EQ(ResolveScratchSliceBytes(300, 256), 512u);
    EXPECT_EQ(ResolveScratchSliceBytes(300, 64), 512u);
}

TEST(ScratchSliceBytes, HonorsADeviceStricterThanTheSpecCeiling)
{
    EXPECT_EQ(ResolveScratchSliceBytes(600, 512), 1024u);
}
