#include <render/RenderQueue.h>

#include <algorithm>
#include <cstring>

uint64_t BuildOpaqueSortKey(const RenderQueueItem& item)
{
    // Key layout (MSB -> LSB):
    // [8b pass][3b pipeline][13b material][20b mesh][4b section][16b depth]
    //
    // The pipeline field is sized to hold every OpaquePipelineId, not merely
    // today's count: a value wider than its field carries into the pass bits
    // above it, which would sort a draw into another pass entirely. The
    // material field gives up the bit, and it is the field that can afford to
    // -- truncation there costs sort quality only, since runs are built from
    // the item fields rather than from the key.
    static_assert(kOpaquePipelineCount <= 8, "pipeline field is 3 bits wide");
    uint32_t depthBits = 0;
    std::memcpy(&depthBits, &item.CameraDepth, sizeof(depthBits));
    return (static_cast<uint64_t>(item.Pass) << 56)
         | (static_cast<uint64_t>(item.Pipeline) << 53)
         | (static_cast<uint64_t>(SlotIndex(item.Material) & 0x1FFFu) << 40)
         | (static_cast<uint64_t>(SlotIndex(item.Mesh) & 0xFFFFFu) << 20)
         | (static_cast<uint64_t>(item.SectionIndex & 0xFu) << 16)
         | (depthBits >> 16);
}

void RenderQueue::Reset()
{
    OpaqueItems.clear();
    TransparentItems.clear();
    OpaqueOrderIndices.clear();
    OpaqueRunList.clear();
}

void RenderQueue::AddTransparent(const RenderQueueItem& item)
{
    // No sort key: the opaque key exists to group state, and grouping is
    // exactly what a blended draw must never do at the cost of order.
    TransparentItems.push_back(item);
}

void BuildTransparentOrder(std::span<const RenderQueueItem> items,
                           const Vec3d& viewPosition,
                           std::vector<uint32_t>& order)
{
    order.clear();
    order.reserve(items.size());
    for (uint32_t index = 0; index < items.size(); ++index)
        order.push_back(index);

    std::sort(order.begin(), order.end(),
              [&](uint32_t leftIndex, uint32_t rightIndex)
              {
                  const Vec3d leftOffset =
                      items[leftIndex].WorldBounds.Center() - viewPosition;
                  const Vec3d rightOffset =
                      items[rightIndex].WorldBounds.Center() - viewPosition;
                  const float left = leftOffset.Dot(leftOffset);
                  const float right = rightOffset.Dot(rightOffset);
                  if (left != right)
                      return left > right; // farther first
                  return leftIndex < rightIndex;
              });
}

void RenderQueue::AddOpaque(const RenderQueueItem& item)
{
    OpaqueItems.push_back(item);
    OpaqueItems.back().SortKey = BuildOpaqueSortKey(OpaqueItems.back());
}

void RenderQueue::SortOpaque()
{
    OpaqueOrderIndices.resize(OpaqueItems.size());

    std::vector<std::pair<uint64_t, uint32_t>>& order = OpaqueSortScratch;
    order.clear();
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
                && item.SkinnedMesh == head.SkinnedMesh
                && item.SectionIndex == head.SectionIndex
                && item.Material == head.Material
                && item.Pass == head.Pass
                && item.LightmapTextureIndex == head.LightmapTextureIndex
                && item.AoTextureIndex == head.AoTextureIndex)
            {
                ++OpaqueRunList.back().Count;
                continue;
            }
        }
        OpaqueRunList.push_back(RenderQueueRun{ .First = i, .Count = 1 });
    }
}
