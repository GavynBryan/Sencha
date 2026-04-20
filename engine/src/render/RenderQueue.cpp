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
         | (static_cast<uint64_t>(item.Material.SlotIndex() & 0xFFFFu) << 32)
         | depthBits;
}

void RenderQueue::Reset()
{
    OpaqueItems.clear();
}

void RenderQueue::AddOpaque(const RenderQueueItem& item)
{
    RenderQueueItem copy = item;
    copy.SortKey = BuildOpaqueSortKey(copy);
    OpaqueItems.push_back(copy);
}

void RenderQueue::SortOpaque()
{
    std::sort(OpaqueItems.begin(), OpaqueItems.end(),
              [](const RenderQueueItem& a, const RenderQueueItem& b) {
                  return a.SortKey < b.SortKey;
              });
}
