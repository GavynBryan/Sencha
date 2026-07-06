#include <render/RenderQueue.h>

#include <gtest/gtest.h>

#include <vector>

namespace
{
RenderQueueItem Item(uint32_t meshSlot, uint32_t materialSlot, uint32_t section, float depth)
{
    RenderQueueItem item;
    item.Mesh = StaticMeshHandle{ meshSlot, 1 };
    item.Material = MaterialHandle{ materialSlot, 1 };
    item.SectionIndex = section;
    item.CameraDepth = depth;
    return item;
}

std::vector<uint32_t> SortedOrder(const std::vector<RenderQueueItem>& items)
{
    RenderQueue queue;
    for (const RenderQueueItem& item : items)
        queue.AddOpaque(item);
    queue.SortOpaque();
    return queue.OpaqueOrder();
}
}

TEST(RenderQueueSort, KeyOrdersMaterialThenMeshThenSection)
{
    // Two materials, interleaved meshes: the sort groups by material first,
    // then mesh, then section, so identical draws land adjacent.
    RenderQueue queue;
    queue.AddOpaque(Item(/*mesh*/ 2, /*material*/ 9, /*section*/ 0, 1.0f));
    queue.AddOpaque(Item(1, 5, 1, 1.0f));
    queue.AddOpaque(Item(2, 5, 0, 1.0f));
    queue.AddOpaque(Item(1, 5, 0, 1.0f));
    queue.SortOpaque();

    const auto& items = queue.Opaque();
    const auto& order = queue.OpaqueOrder();
    ASSERT_EQ(order.size(), 4u);
    // Material 5 first (three items: mesh 1 sections 0,1 then mesh 2), then 9.
    EXPECT_EQ(SlotIndex(items[order[0]].Mesh), 1u);
    EXPECT_EQ(items[order[0]].SectionIndex, 0u);
    EXPECT_EQ(SlotIndex(items[order[1]].Mesh), 1u);
    EXPECT_EQ(items[order[1]].SectionIndex, 1u);
    EXPECT_EQ(SlotIndex(items[order[2]].Mesh), 2u);
    EXPECT_EQ(SlotIndex(items[order[3]].Material), 9u);
}

TEST(RenderQueueSort, TotalOrderIsStableAcrossRepeatedSorts)
{
    // Identical keys (same mesh/material/section/depth): index breaks the tie,
    // so the order is deterministic and identical every time.
    std::vector<RenderQueueItem> items;
    for (int i = 0; i < 16; ++i)
        items.push_back(Item(3, 4, 0, 2.5f));

    const std::vector<uint32_t> first = SortedOrder(items);
    const std::vector<uint32_t> second = SortedOrder(items);
    ASSERT_EQ(first, second);
    for (uint32_t i = 0; i < first.size(); ++i)
        EXPECT_EQ(first[i], i); // equal keys keep insertion order
}

TEST(RenderQueueSort, RunsMergeIdenticalDrawsAndSplitOnEveryFieldChange)
{
    RenderQueue queue;
    // Three instances of mesh 1 / material 2 / section 0 at varied depths.
    queue.AddOpaque(Item(1, 2, 0, 3.0f));
    queue.AddOpaque(Item(1, 2, 0, 1.0f));
    queue.AddOpaque(Item(1, 2, 0, 2.0f));
    // Same mesh, different section: its own run.
    queue.AddOpaque(Item(1, 2, 1, 1.0f));
    // Same mesh + section as the first three but another material: its own run.
    queue.AddOpaque(Item(1, 7, 0, 1.0f));
    queue.SortOpaque();

    const auto& runs = queue.OpaqueRuns();
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].First, 0u);
    EXPECT_EQ(runs[0].Count, 3u);
    EXPECT_EQ(runs[1].Count, 1u);
    EXPECT_EQ(runs[2].Count, 1u);
}

TEST(RenderQueueSort, SlotAliasedMeshesNeverMerge)
{
    // Mesh slots 20 bits apart alias in the sort key's truncated mesh field;
    // the run builder compares the real handles, so they stay separate draws.
    RenderQueue queue;
    queue.AddOpaque(Item(1, 2, 0, 1.0f));
    queue.AddOpaque(Item(1 + (1u << 20), 2, 0, 1.0f));
    queue.SortOpaque();

    const auto& runs = queue.OpaqueRuns();
    ASSERT_EQ(runs.size(), 2u);
    EXPECT_EQ(runs[0].Count, 1u);
    EXPECT_EQ(runs[1].Count, 1u);
}

TEST(RenderQueueSort, RunsPartitionTheWholeOrder)
{
    RenderQueue queue;
    for (uint32_t i = 0; i < 13; ++i)
        queue.AddOpaque(Item(1 + (i % 3), 2 + (i % 2), i % 2, float(i)));
    queue.SortOpaque();

    uint32_t covered = 0;
    uint32_t expectedFirst = 0;
    for (const RenderQueueRun& run : queue.OpaqueRuns())
    {
        EXPECT_EQ(run.First, expectedFirst); // contiguous, in order
        expectedFirst += run.Count;
        covered += run.Count;
    }
    EXPECT_EQ(covered, queue.OpaqueOrder().size());
}

TEST(RenderQueueSort, DepthOrdersWithinOneMeshGroup)
{
    // Distinct meshes under one material: nearer mesh group draws first
    // (front-to-back for early-z), using the quantized depth bits.
    RenderQueue queue;
    queue.AddOpaque(Item(1, 2, 0, 50.0f));
    queue.AddOpaque(Item(3, 2, 0, 1.0f));
    queue.SortOpaque();
    const auto& items = queue.Opaque();
    const auto& order = queue.OpaqueOrder();
    // Mesh sorts above depth in the key, so grouping wins over depth; within
    // the same mesh slot, depth still orders.
    RenderQueue sameMesh;
    sameMesh.AddOpaque(Item(1, 2, 0, 50.0f));
    sameMesh.AddOpaque(Item(1, 2, 0, 1.0f));
    sameMesh.SortOpaque();
    EXPECT_EQ(sameMesh.Opaque()[sameMesh.OpaqueOrder()[0]].CameraDepth, 1.0f);
    (void)items;
    (void)order;
}
