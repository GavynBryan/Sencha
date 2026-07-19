#include <render/RenderQueue.h>

#include <algorithm>
#include <cstring>

uint64_t BuildOpaqueSortKey(const RenderQueueItem& item)
{
    // Key layout (MSB -> LSB):
    // [8b pass][2b pipeline][14b material][20b mesh][4b section][16b depth]
    uint32_t depthBits = 0;
    std::memcpy(&depthBits, &item.CameraDepth, sizeof(depthBits));
    return (static_cast<uint64_t>(item.Pass) << 56)
         | (static_cast<uint64_t>(item.Pipeline) << 54)
         | (static_cast<uint64_t>(SlotIndex(item.Material) & 0x3FFFu) << 40)
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
    OpaqueOrderIndices.resize(OpaqueItems.size());

    std::vector<std::pair<uint64_t, uint32_t>> order;
    order.reserve(OpaqueItems.size());
    for (uint32_t i = 0; i < OpaqueItems.size(); ++i)
        order.emplace_back(OpaqueItems[i].SortKey, i);

    std::sort(order.begin(), order.end());

    for (size_t i = 0; i < order.size(); ++i)
        OpaqueOrderIndices[i] = order[i].second;

    OpaqueRunList.clear();
    for (uint32_t i = 0; i < OpaqueOrderIndices.size(); ++i)
    {
        const RenderQueueItem& item = OpaqueItems[OpaqueOrderIndices[i]];
        if (!OpaqueRunList.empty())
        {
            const RenderQueueItem& head =
                OpaqueItems[OpaqueOrderIndices[OpaqueRunList.back().First]];
            if (item.Pipeline == head.Pipeline
                && item.Mesh == head.Mesh
                && item.SectionIndex == head.SectionIndex
                && item.Material == head.Material
                && item.Pass == head.Pass
                && item.LightmapTextureIndex == head.LightmapTextureIndex)
            {
                ++OpaqueRunList.back().Count;
                continue;
            }
        }
        OpaqueRunList.push_back(RenderQueueRun{ .First = i, .Count = 1 });
    }
}
