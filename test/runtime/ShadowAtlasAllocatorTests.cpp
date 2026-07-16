#include <gtest/gtest.h>

#include <render/RenderLight.h>
#include <render/ShadowAtlasAllocator.h>

TEST(ShadowAtlasAllocator, FirstFitPlacementIsDeterministic)
{
    ShadowAtlasAllocator atlas;
    const ShadowAtlasAllocation first = atlas.Allocate(512);
    const ShadowAtlasAllocation second = atlas.Allocate(512);
    const ShadowAtlasAllocation big = atlas.Allocate(1024);

    EXPECT_EQ(first, (ShadowAtlasAllocation{ 0, 0, 512 }));
    EXPECT_EQ(second, (ShadowAtlasAllocation{ 512, 0, 512 }));
    // Both 512 tiles live inside the first 1024 quadrant, so the 1024 tile
    // lands in the next quadrant over.
    EXPECT_EQ(big, (ShadowAtlasAllocation{ 1024, 0, 1024 }));
    EXPECT_EQ(atlas.AllocatedCount(), 3u);
}

TEST(ShadowAtlasAllocator, ExhaustsAndReusesTier)
{
    ShadowAtlasAllocator atlas;
    ShadowAtlasAllocation allocations[16];
    for (ShadowAtlasAllocation& allocation : allocations)
    {
        allocation = atlas.Allocate(512);
        EXPECT_TRUE(allocation.IsValid());
    }
    EXPECT_FALSE(atlas.Allocate(512).IsValid());
    EXPECT_FALSE(atlas.Allocate(256).IsValid());

    atlas.Free(allocations[5]);
    const ShadowAtlasAllocation reused = atlas.Allocate(512);
    EXPECT_EQ(reused, allocations[5]);
}

TEST(ShadowAtlasAllocator, FreeingChildrenReopensTheParentTier)
{
    ShadowAtlasAllocator atlas;
    ShadowAtlasAllocation children[4];
    for (ShadowAtlasAllocation& child : children)
        child = atlas.Allocate(512);
    // The four 512 tiles fill quadrant zero, pushing a 1024 to quadrant one.
    EXPECT_EQ(atlas.Allocate(1024), (ShadowAtlasAllocation{ 1024, 0, 1024 }));

    for (const ShadowAtlasAllocation& child : children)
        atlas.Free(child);
    EXPECT_EQ(atlas.Allocate(1024), (ShadowAtlasAllocation{ 0, 0, 1024 }));
}

TEST(ShadowAtlasAllocator, SmallTilesPackAroundLargeOnes)
{
    ShadowAtlasAllocator atlas;
    (void)atlas.Allocate(1024);
    const ShadowAtlasAllocation small = atlas.Allocate(256);
    EXPECT_EQ(small, (ShadowAtlasAllocation{ 1024, 0, 256 }));
    EXPECT_EQ(atlas.AllocatedTexels(), 1024u * 1024u + 256u * 256u);
}

TEST(ShadowAtlasAllocator, RejectsInvalidTileSizes)
{
    ShadowAtlasAllocator atlas;
    EXPECT_FALSE(atlas.Allocate(128).IsValid());
    EXPECT_FALSE(atlas.Allocate(513).IsValid());
    EXPECT_FALSE(atlas.Allocate(4096).IsValid());
    EXPECT_FALSE(atlas.Allocate(0).IsValid());
}

TEST(ShadowAtlasAllocator, InsetScaleBiasMatchesTheFixedGridForFullTiles)
{
    // The fixed-grant grid the editor still uses is the special case of a
    // 512 tile at slot-major placement; both paths must express the same
    // logical interior.
    const ShadowAtlasAllocation allocation{ 512, 0, 512 };
    const Vec4 fromAllocator = ShadowAtlasAllocator::InsetScaleBias(allocation);
    const Vec4 fromFixedGrid = SpotShadowAtlasScaleBias(1u);
    EXPECT_EQ(fromAllocator, fromFixedGrid);
}

TEST(ShadowAtlasAllocator, InsetScaleBiasKeepsGuardOnEveryTier)
{
    const ShadowAtlasAllocation small{ 1024, 512, 256 };
    const Vec4 scaleBias = ShadowAtlasAllocator::InsetScaleBias(small);
    constexpr float atlas = static_cast<float>(kSpotShadowAtlasExtent);
    EXPECT_FLOAT_EQ(scaleBias.X, (256.0f - 16.0f) / atlas);
    EXPECT_FLOAT_EQ(scaleBias.Z, (1024.0f + 8.0f) / atlas);
    EXPECT_FLOAT_EQ(scaleBias.W, (512.0f + 8.0f) / atlas);
}
