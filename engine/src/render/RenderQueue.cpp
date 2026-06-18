#include <render/RenderQueue.h>

#include <algorithm>
#include <cstring>

uint64_t BuildOpaqueSortKey(const RenderQueueItem& item)
{
    // Key layout (MSB -> LSB): [8b pass][16b material][32b depth]
    // Depth bits are reinterpreted as uint so positive floats sort front-to-back.
    uint32_t depthBits = 0;
    std::memcpy(&depthBits, &item.CameraDepth, sizeof(depthBits));
    return (static_cast<uint64_t>(item.Pass) << 56)
         | (static_cast<uint64_t>(SlotIndex(item.Material) & 0xFFFFu) << 32)
         | depthBits;
}

void RenderQueue::Reset()
{
    OpaqueItems.clear();
    OpaqueOrderIndices.clear();
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
    // OpaqueOrder() and index back into OpaqueItems.
    OpaqueOrderIndices.resize(OpaqueItems.size());

    std::vector<std::pair<uint64_t, uint32_t>> order;
    order.reserve(OpaqueItems.size());
    for (uint32_t i = 0; i < OpaqueItems.size(); ++i)
        order.emplace_back(OpaqueItems[i].SortKey, i);

    std::sort(order.begin(), order.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });

    for (size_t i = 0; i < order.size(); ++i)
        OpaqueOrderIndices[i] = order[i].second;
}
