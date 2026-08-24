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

TEST(RenderQueueRuns, DifferentSkinnedMeshesNeverMergeIntoOneRun)
{
    // Skinned items carry a null static handle, so the static-mesh equality
    // that splits every other pair is blind to them: two different skinned
    // meshes with the same material and pipeline agree on every static field.
    // Only the skinned handle in the run-merge identity keeps them apart --
    // merged, one of them would draw with the other's geometry.
    RenderQueueItem first;
    first.SkinnedMesh = SkinnedMeshHandle{ 7, 1 };
    RenderQueueItem second;
    second.SkinnedMesh = SkinnedMeshHandle{ 8, 1 };

    RenderQueue queue;
    queue.AddOpaque(first);
    queue.AddOpaque(second);
    queue.SortOpaque();

    ASSERT_EQ(queue.OpaqueRuns().size(), 2u)
        << "two different skinned meshes collapsed into one instanced draw";
}

TEST(RenderQueueRuns, PosedSkinnedInstancesNeverMerge)
{
    // Two entities sharing one skinned mesh pose independently: each draws
    // from its own posed vertex buffer, so merging them into one instanced
    // draw would render both with whichever pose won. Their pose slots are
    // the only difference, and that alone must split the run.
    RenderQueue queue;
    for (std::uint32_t slot = 0; slot < 2; ++slot)
    {
        RenderQueueItem item{};
        item.SkinnedMesh = SkinnedMeshHandle{ 4, 1 };
        item.Material = MaterialHandle{ 1, 1 };
        item.PoseSlot = slot;
        queue.AddOpaque(item);
    }
    queue.SortOpaque();
    EXPECT_EQ(queue.OpaqueRuns().size(), 2u);

    // Unposed skinned items (no pose produced this frame) share rest
    // geometry and still merge, which is the pre-pose behavior.
    RenderQueue rest;
    for (std::uint32_t i = 0; i < 2; ++i)
    {
        RenderQueueItem item{};
        item.SkinnedMesh = SkinnedMeshHandle{ 4, 1 };
        item.Material = MaterialHandle{ 1, 1 };
        rest.AddOpaque(item);
    }
    rest.SortOpaque();
    ASSERT_EQ(rest.OpaqueRuns().size(), 1u);
    EXPECT_EQ(rest.OpaqueRuns()[0].Count, 2u);
}
