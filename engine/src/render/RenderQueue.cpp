#include <render/RenderQueue.h>

#include <algorithm>
#include <cstring>

uint64_t BuildOpaqueSortKey(const RenderQueueItem& item)
{
    // Key layout (MSB -> LSB): [8b pass][16b material][20b mesh][4b section][16b depth]
    // Material first (the expensive state change), then mesh + section so
    // identical draws land adjacent and merge into instanced runs. Depth is the
    // top 16 bits of the float pattern (monotonic for non-negative floats):
    // front-to-back ordering coarsens only WITHIN one mesh+section group, which
    // is exactly the set that collapses into a single draw anyway.
    uint32_t depthBits = 0;
    std::memcpy(&depthBits, &item.CameraDepth, sizeof(depthBits));
    return (static_cast<uint64_t>(item.Pass) << 56)
         | (static_cast<uint64_t>(SlotIndex(item.Material) & 0xFFFFu) << 40)
         | (static_cast<uint64_t>(SlotIndex(item.Mesh) & 0xFFFFFu) << 20)
         | (static_cast<uint64_t>(item.SectionIndex & 0xFu) << 16)
         | (depthBits >> 16);
}

void RenderQueue::Reset()
{
    OpaqueItems.clear();
    OpaqueOrderIndices.clear();
    OpaqueRunList.clear();
}

void RenderQueue::AddOpaque(const RenderQueueItem& item)
{
    OpaqueItems.push_back(item);
    OpaqueItems.back().SortKey = BuildOpaqueSortKey(OpaqueItems.back());
}

void RenderQueue::SortOpaque()
{
    // Sort an order array of (SortKey, item index) pairs rather than the items
    // themselves: each item is 128 bytes but the comparator only reads the
    // 8-byte key, so moving items during the sort is pure waste. Consumers walk
    // OpaqueOrder() and index back into OpaqueItems. Ties break on the item
    // index: a total order, so the draw order is identical across runs and
    // across serial/parallel extraction for the same items.
    OpaqueOrderIndices.resize(OpaqueItems.size());

    std::vector<std::pair<uint64_t, uint32_t>> order;
    order.reserve(OpaqueItems.size());
    for (uint32_t i = 0; i < OpaqueItems.size(); ++i)
        order.emplace_back(OpaqueItems[i].SortKey, i);

    std::sort(order.begin(), order.end());

    for (size_t i = 0; i < order.size(); ++i)
        OpaqueOrderIndices[i] = order[i].second;

    // Instanced runs: adjacent entries drawing the same mesh section with the
    // same material merge. Compared on the real fields, not the key, so a
    // slot-aliased key can never merge two different meshes.
    OpaqueRunList.clear();
    for (uint32_t i = 0; i < OpaqueOrderIndices.size(); ++i)
    {
        const RenderQueueItem& item = OpaqueItems[OpaqueOrderIndices[i]];
        if (!OpaqueRunList.empty())
        {
            const RenderQueueItem& head =
                OpaqueItems[OpaqueOrderIndices[OpaqueRunList.back().First]];
            if (item.Mesh == head.Mesh && item.SectionIndex == head.SectionIndex
                && item.Material == head.Material && item.Pass == head.Pass)
            {
                ++OpaqueRunList.back().Count;
                continue;
            }
        }
        OpaqueRunList.push_back(RenderQueueRun{ .First = i, .Count = 1 });
    }
}
