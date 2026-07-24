#include <gtest/gtest.h>

#include <render/RenderQueue.h>

namespace
{
    RenderQueueItem MakeItem(std::uint32_t lightmapIndex, const Vec4& scaleBias)
    {
        RenderQueueItem item{};
        item.Mesh = StaticMeshHandle{ 1, 1 };
        item.Material = MaterialHandle{ 1, 1 };
        item.LightmapTextureIndex = lightmapIndex;
        item.LightmapScaleBias = scaleBias;
        return item;
    }
}

TEST(RenderQueueRuns, ScaleBiasVariesWithinOneRun)
{
    // Per-instance data never splits a run: three placements with distinct
    // atlas rects still collapse into one instanced draw.
    RenderQueue queue;
    queue.AddOpaque(MakeItem(7, Vec4{ 0.1f, 0.1f, 0.0f, 0.0f }));
    queue.AddOpaque(MakeItem(7, Vec4{ 0.1f, 0.1f, 0.5f, 0.0f }));
    queue.AddOpaque(MakeItem(7, Vec4{ 0.1f, 0.1f, 0.0f, 0.5f }));
    queue.SortOpaque();

    ASSERT_EQ(queue.OpaqueRuns().size(), 1u);
    EXPECT_EQ(queue.OpaqueRuns()[0].Count, 3u);
}

TEST(RenderQueueRuns, DifferentAtlasIndicesSplitRuns)
{
    // The atlas index rides a per-draw push constant, so the same mesh
    // resident in two zones (two atlases) must not share a run.
    RenderQueue queue;
    queue.AddOpaque(MakeItem(7, Vec4{ 1, 1, 0, 0 }));
    queue.AddOpaque(MakeItem(9, Vec4{ 1, 1, 0, 0 }));
    queue.AddOpaque(MakeItem(7, Vec4{ 1, 1, 0, 0 }));
    queue.SortOpaque();

    // Sorting is by sort key (which ignores the index), so equal-keyed items
    // stay adjacent; the field-equality merge must still split them.
    std::uint32_t totalRuns = static_cast<std::uint32_t>(queue.OpaqueRuns().size());
    EXPECT_GE(totalRuns, 2u);
    std::uint32_t totalItems = 0;
    for (const RenderQueueRun& run : queue.OpaqueRuns())
        totalItems += run.Count;
    EXPECT_EQ(totalItems, 3u);
}
